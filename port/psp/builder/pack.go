package main

// EBOOT packing: a Go copy of port/psp/pack/psp_pack.py, plus the XMB art
// sections pack-pbp would have written from xmb_art.py's PNGs.

import (
	"bufio"
	"crypto/sha1"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"strings"
)

// The files the PSP build reads from the disc: the executable (EE data the
// port fills from it) and the four AFS archives.
var keep = []struct{ path, sha1 string }{
	{"/SLUS_204.86", "dd8558a04891b0b1472ea1d8ae2bb84947ae8937"},
	{"/PS2/AFS00.AFS", "b703d7a41c47006fb109ccf81aff3cc5ad920ab1"},
	{"/PS2/AFS01.AFS", "175728f45aff10006f1c87224e6e919d9af78b95"},
	{"/PS2/AFS02.AFS", "7593be944b1a026e4b74fd54a5ee60aa6699f568"},
	{"/PS2/AFS03.AFS", "95fb23891755f9c666f8756c6c731efc081e5696"},
}

var (
	errNotISO  = errors.New("not an ISO9660 disc image")
	errBadData = errors.New("unexpected data on the disc")
	errHash    = errors.New("the game data on this disc does not match the USA retail release (a bad or modified dump?)")
)

const (
	secICON0 = 1
	secPIC1  = 4
	secPSAR  = 7
)

// pbpSections splits an EBOOT.PBP into its eight sections.
func pbpSections(pbp []byte) ([8][]byte, error) {
	var s [8][]byte
	if len(pbp) < 40 || string(pbp[:4]) != "\x00PBP" {
		return s, errors.New("base EBOOT.PBP is not a PSP EBOOT")
	}
	var offs [9]uint32
	for i := 0; i < 8; i++ {
		offs[i] = le32(pbp, 8+4*i)
	}
	offs[8] = uint32(len(pbp))
	for i := 0; i < 8; i++ {
		if offs[i] > offs[i+1] || offs[i+1] > uint32(len(pbp)) {
			return s, errors.New("base EBOOT.PBP is damaged")
		}
		s[i] = pbp[offs[i]:offs[i+1]]
	}
	return s, nil
}

func both32(v uint32) []byte {
	b := make([]byte, 8)
	binary.LittleEndian.PutUint32(b, v)
	binary.BigEndian.PutUint32(b[4:], v)
	return b
}

func both16(v uint16) []byte {
	b := make([]byte, 4)
	binary.LittleEndian.PutUint16(b, v)
	binary.BigEndian.PutUint16(b[2:], v)
	return b
}

func dirrec(lba, size uint32, name []byte, dir bool) []byte {
	ln := 33 + len(name)
	ln += ln & 1
	r := []byte{byte(ln), 0}
	r = append(r, both32(lba)...)
	r = append(r, both32(size)...)
	r = append(r, make([]byte, 7)...)
	f := byte(0)
	if dir {
		f = 2
	}
	r = append(r, f, 0, 0)
	r = append(r, both16(1)...)
	r = append(r, byte(len(name)))
	r = append(r, name...)
	return append(r, make([]byte, ln-len(r))...)
}

func pad(b []byte, n int) []byte { return append(b, make([]byte, n-len(b))...) }

// writeMiniISO writes the keep files from the disc as a minimal ISO9660
// image, checking each file's SHA-1 as it goes.
func writeMiniISO(w io.Writer, d *iso, progress func(done, total int64)) error {
	const rootLBA, ps2LBA = 18, 19
	lba := uint32(20)
	var lbas []uint32
	var total int64
	for _, k := range keep {
		e, ok := d.files[k.path]
		if !ok {
			return fmt.Errorf("no %s on the disc", k.path)
		}
		lbas = append(lbas, lba)
		lba += (e.size + sector - 1) / sector
		total += int64(e.size)
	}
	root := append(dirrec(rootLBA, sector, []byte{0}, true), dirrec(rootLBA, sector, []byte{1}, true)...)
	root = append(root, dirrec(ps2LBA, sector, []byte("PS2"), true)...)
	ps2 := append(dirrec(ps2LBA, sector, []byte{0}, true), dirrec(rootLBA, sector, []byte{1}, true)...)
	for i, k := range keep {
		rec := dirrec(lbas[i], d.files[k.path].size, []byte(k.path[strings.LastIndex(k.path, "/")+1:]+";1"), false)
		if strings.HasPrefix(k.path, "/PS2/") {
			ps2 = append(ps2, rec...)
		} else {
			root = append(root, rec...)
		}
	}
	pvd := make([]byte, sector)
	copy(pvd, "\x01CD001\x01\x00")
	copy(pvd[40:72], fmt.Sprintf("%-32s", "MVC2_PSP"))
	copy(pvd[80:], both32(lba))
	copy(pvd[120:], both16(1))
	copy(pvd[124:], both16(1))
	copy(pvd[128:], both16(sector))
	copy(pvd[156:], dirrec(rootLBA, sector, []byte{0}, true))
	pvd[881] = 1
	term := make([]byte, sector)
	copy(term, "\xffCD001\x01")
	for _, b := range [][]byte{make([]byte, 16*sector), pvd, term, pad(root, sector), pad(ps2, sector)} {
		if _, err := w.Write(b); err != nil {
			return err
		}
	}
	var done int64
	buf := make([]byte, 1<<20)
	for _, k := range keep {
		e := d.files[k.path]
		h := sha1.New()
		r := io.NewSectionReader(d.f, int64(e.lba)*sector, int64(e.size))
		for {
			n, err := r.Read(buf)
			if n > 0 {
				h.Write(buf[:n])
				if _, werr := w.Write(buf[:n]); werr != nil {
					return werr
				}
				done += int64(n)
				progress(done, total)
			}
			if err == io.EOF {
				break
			}
			if err != nil {
				return err
			}
		}
		if hex.EncodeToString(h.Sum(nil)) != k.sha1 {
			return errHash
		}
		if e.size%sector != 0 {
			if _, err := w.Write(make([]byte, sector-e.size%sector)); err != nil {
				return err
			}
		}
	}
	return nil
}

// build writes the finished EBOOT: base sections with ICON0/PIC1 replaced,
// then the game data as DATA.PSAR at a sector boundary.
func build(base []byte, d *iso, out string, progress func(done, total int64)) error {
	s, err := pbpSections(base)
	if err != nil {
		return err
	}
	a, err := openAFS(d, 2)
	if err != nil {
		return err
	}
	if s[secICON0], s[secPIC1], err = xmbArt(a); err != nil {
		return err
	}
	head := make([]byte, 40)
	copy(head, base[:8])
	off := uint32(40)
	for i := 0; i < 7; i++ {
		binary.LittleEndian.PutUint32(head[8+4*i:], off)
		off += uint32(len(s[i]))
	}
	padding := (sector - off%sector) % sector
	binary.LittleEndian.PutUint32(head[8+4*secPSAR:], off+padding)

	tmp := out + ".part"
	f, err := os.Create(tmp)
	if err != nil {
		return err
	}
	w := bufio.NewWriterSize(f, 1<<20)
	err = func() error {
		if _, err := w.Write(head); err != nil {
			return err
		}
		for i := 0; i < 7; i++ {
			if _, err := w.Write(s[i]); err != nil {
				return err
			}
		}
		if _, err := w.Write(make([]byte, padding)); err != nil {
			return err
		}
		if err := writeMiniISO(w, d, progress); err != nil {
			return err
		}
		return w.Flush()
	}()
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		os.Remove(tmp)
		return err
	}
	return os.Rename(tmp, out)
}
