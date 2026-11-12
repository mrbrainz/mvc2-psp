package main

// ISO9660, AFS, LZ16 and TIM2 readers: Go copies of port/tools/convert/
// iso.py, afs.py, tim2.py, textures.py and port/tools/mvc2_lz16.py.

import (
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"strings"
)

const sector = 2048

type extent struct{ lba, size uint32 }

type iso struct {
	f     *os.File
	files map[string]extent
}

func openISO(path string) (*iso, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	d := &iso{f: f, files: map[string]extent{}}
	pvd, err := d.sectors(16, 1)
	if err != nil || string(pvd[1:6]) != "CD001" {
		f.Close()
		return nil, errNotISO
	}
	root := pvd[156 : 156+34]
	if err := d.walk(le32(root, 2), le32(root, 10), "", 0); err != nil {
		f.Close()
		return nil, err
	}
	return d, nil
}

func (d *iso) sectors(lba, n uint32) ([]byte, error) {
	b := make([]byte, int(n)*sector)
	_, err := d.f.ReadAt(b, int64(lba)*sector)
	if err == io.EOF {
		err = nil
	}
	return b, err
}

func (d *iso) walk(lba, size uint32, prefix string, depth int) error {
	if depth > 8 {
		return nil
	}
	data, err := d.sectors(lba, (size+sector-1)/sector)
	if err != nil {
		return err
	}
	for p := 0; p < len(data); {
		ln := int(data[p])
		if ln == 0 {
			p = (p/sector + 1) * sector
			continue
		}
		if p+33 > len(data) {
			break
		}
		ext, sz, flags, nl := le32(data, p+2), le32(data, p+10), data[p+25], int(data[p+32])
		if p+33+nl > len(data) {
			break
		}
		name := data[p+33 : p+33+nl]
		p += ln
		if nl == 1 && (name[0] == 0 || name[0] == 1) {
			continue
		}
		full := prefix + "/" + strings.SplitN(string(name), ";", 2)[0]
		if flags&2 != 0 {
			if err := d.walk(ext, sz, full, depth+1); err != nil {
				return err
			}
		} else {
			d.files[strings.ToUpper(full)] = extent{ext, sz}
		}
	}
	return nil
}

func (d *iso) read(path string, off, size int64) ([]byte, error) {
	e, ok := d.files[strings.ToUpper(path)]
	if !ok {
		return nil, fmt.Errorf("no %s on the disc", path)
	}
	if size < 0 {
		size = int64(e.size) - off
	}
	b := make([]byte, size)
	_, err := d.f.ReadAt(b, int64(e.lba)*sector+off)
	return b, err
}

type afsEntry struct {
	off, size uint32
	name      string
}

type afs struct {
	d       *iso
	path    string
	entries []afsEntry
}

func openAFS(d *iso, ptid int) (*afs, error) {
	a := &afs{d: d, path: fmt.Sprintf("/PS2/AFS%02d.AFS", ptid)}
	hdr, err := d.read(a.path, 0, 8)
	if err != nil {
		return nil, err
	}
	if string(hdr[:4]) != "AFS\x00" {
		return nil, fmt.Errorf("%s: bad AFS header", a.path)
	}
	n := int(le32(hdr, 4))
	tab, err := d.read(a.path, 8, int64(8*(n+1)))
	if err != nil {
		return nil, err
	}
	ntOff, ntSize := le32(tab, 8*n), le32(tab, 8*n+4)
	var nt []byte
	if ntOff != 0 && int(ntSize) >= 48*n {
		if nt, err = d.read(a.path, int64(ntOff), int64(ntSize)); err != nil {
			return nil, err
		}
	}
	for i := 0; i < n; i++ {
		e := afsEntry{off: le32(tab, 8*i), size: le32(tab, 8*i+4)}
		if nt != nil {
			raw := nt[48*i : 48*i+32]
			if z := indexByte(raw, 0); z >= 0 {
				raw = raw[:z]
			}
			e.name = latin1(raw)
		}
		a.entries = append(a.entries, e)
	}
	return a, nil
}

func (a *afs) find(name string) ([]byte, error) {
	for _, e := range a.entries {
		if strings.EqualFold(e.name, name) {
			return a.d.read(a.path, int64(e.off), int64(e.size))
		}
	}
	return nil, fmt.Errorf("%s: no %s", a.path, name)
}

// lz16 decodes the 16-bit-word LZ of the *TEX.BIN packs.
func lz16(src []byte) []byte {
	w := make([]uint16, len(src)/2)
	for i := range w {
		w[i] = binary.LittleEndian.Uint16(src[2*i:])
	}
	var out []uint16
	i, mask, bits := 0, uint16(0), uint16(0)
	next := func() uint16 {
		if i >= len(w) {
			return 0
		}
		v := w[i]
		i++
		return v
	}
	for {
		if mask == 0 {
			bits, mask = next(), 0x8000
		}
		if bits&mask != 0 {
			v := next()
			cnt := int(v >> 11)
			if cnt != 0 {
				v &= 0x7FF
			} else {
				cnt = int(next())
			}
			if v == 0 {
				if cnt == 0 {
					break
				}
				out = append(out, make([]uint16, cnt)...)
			} else {
				s := len(out) - int(v)
				if s < 0 {
					break
				}
				for k := 0; k < cnt; k++ {
					out = append(out, out[s+k])
				}
			}
		} else {
			if i >= len(w) {
				break
			}
			out = append(out, next())
		}
		mask >>= 1
	}
	b := make([]byte, 2*len(out))
	for k, v := range out {
		binary.LittleEndian.PutUint16(b[2*k:], v)
	}
	return b
}

type texRecord struct{ flags, off uint32 }

func texRecords(d []byte) []texRecord {
	var out []texRecord
	for q := 0; q+16 <= len(d) && int16(binary.LittleEndian.Uint16(d[q:])) > 0; q += 16 {
		out = append(out, texRecord{le32(d, q+4), le32(d, q+8)})
	}
	return out
}

func looksLikePack(d []byte) bool {
	head := d
	if len(head) > 4096 {
		head = head[:4096]
	}
	r := texRecords(head)
	if len(r) == 0 {
		return false
	}
	tim := false
	for _, t := range r {
		if t.flags>>16 > 3 || int(t.off) >= len(d) {
			return false
		}
		if t.flags>>16 == 1 && int(t.off)+4 <= len(d) && string(d[t.off:t.off+4]) == "TIM2" {
			tim = true
		}
	}
	return tim
}

// packPic: picture 0 of TIM2 record idx in a *TEX.BIN pack.
func packPic(raw []byte, idx int) (*img, error) {
	d := raw
	if !looksLikePack(raw) {
		d = lz16(raw)
	}
	r := texRecords(d)
	if idx >= len(r) {
		return nil, errBadData
	}
	return tim2(d, int(r[idx].off))
}

func tim2Color(fmt int, b []byte, o int) [4]byte {
	switch fmt {
	case 1, 16:
		c := uint16(b[o]) | uint16(b[o+1])<<8
		a := byte(0)
		if c&0x8000 != 0 {
			a = 255
		}
		return [4]byte{byte(c&0x1F) << 3, byte((c>>5)&0x1F) << 3, byte((c>>10)&0x1F) << 3, a}
	case 2, 24:
		return [4]byte{b[o], b[o+1], b[o+2], 255}
	}
	a := int(b[o+3]) * 2
	if a > 255 {
		a = 255
	}
	return [4]byte{b[o], b[o+1], b[o+2], byte(a)}
}

// tim2 decodes the first picture of the TIM2 file at base.
func tim2(data []byte, base int) (m *img, err error) {
	defer func() {
		if recover() != nil {
			m, err = nil, errBadData
		}
	}()
	if string(data[base:base+4]) != "TIM2" {
		return nil, errBadData
	}
	p := base + 16
	if data[base+5] == 1 {
		p = base + 128
	}
	imgSize := int(le32(data, p+8))
	hdrSize := int(binary.LittleEndian.Uint16(data[p+12:]))
	clutColors := int(binary.LittleEndian.Uint16(data[p+14:]))
	clutType, imgType := int(data[p+18]), int(data[p+19])
	w, h := int(binary.LittleEndian.Uint16(data[p+20:])), int(binary.LittleEndian.Uint16(data[p+22:]))
	imgOff := p + hdrSize
	clutOff := imgOff + imgSize
	var clut [][4]byte
	if clutColors > 0 {
		ef := clutType & 0x3F
		es := map[int]int{1: 2, 2: 3, 3: 4}[ef]
		if es == 0 {
			es = 4
		}
		raw := make([][4]byte, clutColors)
		for k := range raw {
			raw[k] = tim2Color(ef, data, clutOff+es*k)
		}
		clut = raw
		if clutColors == 256 && clutType&0x80 == 0 {
			clut = make([][4]byte, 256)
			for k := range clut {
				clut[k] = raw[(k&0xE7)|((k&0x08)<<1)|((k&0x10)>>1)]
			}
		}
	}
	rgba := make([]byte, w*h*4)
	switch imgType {
	case 4, 5:
		for k := 0; k < w*h; k++ {
			var i int
			if imgType == 5 {
				i = int(data[imgOff+k])
			} else if k%2 == 0 {
				i = int(data[imgOff+k/2] & 0xF)
			} else {
				i = int(data[imgOff+k/2] >> 4)
			}
			c := [4]byte{255, 0, 255, 255}
			if i < len(clut) {
				c = clut[i]
			}
			copy(rgba[4*k:], c[:])
		}
	default:
		es := map[int]int{1: 2, 2: 3, 3: 4}[imgType]
		if es == 0 {
			return nil, errBadData
		}
		for k := 0; k < w*h; k++ {
			c := tim2Color(imgType, data, imgOff+es*k)
			copy(rgba[4*k:], c[:])
		}
	}
	return fromRGBA(w, h, rgba), nil
}

func le32(b []byte, o int) uint32 { return binary.LittleEndian.Uint32(b[o:]) }

func indexByte(b []byte, c byte) int {
	for i, v := range b {
		if v == c {
			return i
		}
	}
	return -1
}

func latin1(b []byte) string {
	r := make([]rune, len(b))
	for i, v := range b {
		r[i] = rune(v)
	}
	return string(r)
}
