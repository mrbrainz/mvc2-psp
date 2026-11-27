package main

// Disc image containers, all read in place as a 2048-byte-sector view:
// .iso, raw .bin (2352-byte sectors, mode 1 or mode 2 form 1), .cue (its
// data track's file), CSO/ZSO (CISO v1/v2 deflate or LZ4, ZISO LZ4) and a
// .zip holding any of these (a stored member in place, a deflated one
// extracted to a temporary file first).

import (
	"archive/zip"
	"bufio"
	"bytes"
	"compress/flate"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"path"
	"path/filepath"
	"strings"
	"sync"
)

type source interface {
	io.ReaderAt
	io.Closer
}

type closers struct {
	io.ReaderAt
	close []func() error
}

func (c *closers) Close() error {
	var err error
	for i := len(c.close) - 1; i >= 0; i-- {
		if e := c.close[i](); err == nil {
			err = e
		}
	}
	return err
}

// openImage opens any supported container and returns its 2048-byte view.
func openImage(p string) (source, error) {
	if strings.EqualFold(filepath.Ext(p), ".cue") {
		cue, err := os.ReadFile(p)
		if err != nil {
			return nil, err
		}
		name, err := cueDataFile(cue)
		if err != nil {
			return nil, err
		}
		p = filepath.Join(filepath.Dir(p), name)
	}
	f, err := os.Open(p)
	if err != nil {
		return nil, err
	}
	st, err := f.Stat()
	if err != nil {
		f.Close()
		return nil, err
	}
	c := &closers{f, []func() error{f.Close}}
	s, err := container(f, st.Size(), c)
	if err != nil {
		c.Close()
		return nil, err
	}
	return s, nil
}

// container identifies what r holds and wraps it; c collects what to close.
func container(r io.ReaderAt, size int64, c *closers) (source, error) {
	head := make([]byte, 16)
	r.ReadAt(head, 0)
	switch {
	case bytes.HasPrefix(head, []byte("CISO")) || bytes.HasPrefix(head, []byte("ZISO")):
		z, err := newCSO(r)
		if err != nil {
			return nil, err
		}
		c.ReaderAt = z
		return c, nil
	case bytes.HasPrefix(head, []byte("PK\x03\x04")):
		return fromZip(r, size, c)
	case bytes.HasPrefix(head, []byte("MComprHD")):
		return nil, errors.New("that's a CHD image; convert it to an .iso first (chdman extractcd)")
	case bytes.HasPrefix(head, []byte("7z\xbc\xaf")) || bytes.HasPrefix(head, []byte("Rar!")):
		return nil, errors.New("that's a 7z/RAR archive; extract the disc image from it (or zip it) first")
	}
	c.ReaderAt = frame(r)
	return c, nil
}

// frame finds the sector layout from the volume descriptor at sector 16.
func frame(r io.ReaderAt) io.ReaderAt {
	id := make([]byte, 6)
	if n, _ := r.ReadAt(id, 16*sector); n == 6 && string(id[1:6]) == "CD001" {
		return r
	}
	for _, hdr := range []int64{16, 24} {
		if n, _ := r.ReadAt(id, 16*2352+hdr); n == 6 && string(id[1:6]) == "CD001" {
			return &rawCD{r, hdr}
		}
	}
	return r // not ISO9660: openISO says so
}

type rawCD struct {
	r   io.ReaderAt
	hdr int64
}

func (d *rawCD) ReadAt(p []byte, off int64) (int, error) {
	done := 0
	for done < len(p) {
		in := off % sector
		k := int(sector - in)
		if k > len(p)-done {
			k = len(p) - done
		}
		n, err := d.r.ReadAt(p[done:done+k], off/sector*2352+d.hdr+in)
		done += n
		off += int64(n)
		if n < k {
			if err == nil {
				err = io.EOF
			}
			return done, err
		}
	}
	return done, nil
}

// cueDataFile: the FILE of the cue sheet's first data (MODE1/MODE2) track.
func cueDataFile(cue []byte) (string, error) {
	file := ""
	sc := bufio.NewScanner(bytes.NewReader(cue))
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		f := strings.Fields(line)
		switch {
		case len(f) >= 2 && strings.EqualFold(f[0], "FILE"):
			rest := strings.TrimSpace(line[4:])
			if strings.HasPrefix(rest, `"`) {
				if e := strings.Index(rest[1:], `"`); e >= 0 {
					file = rest[1 : 1+e]
					break
				}
			}
			file = f[1]
		case len(f) >= 3 && strings.EqualFold(f[0], "TRACK") && strings.HasPrefix(strings.ToUpper(f[2]), "MODE") && file != "":
			return file, nil
		}
	}
	return "", errors.New("the .cue sheet names no data track")
}

func fromZip(r io.ReaderAt, size int64, c *closers) (source, error) {
	z, err := zip.NewReader(r, size)
	if err != nil {
		return nil, err
	}
	var pick *zip.File
	for _, f := range z.File {
		if strings.EqualFold(path.Ext(f.Name), ".cue") {
			rc, err := f.Open()
			if err != nil {
				return nil, err
			}
			cue, err := io.ReadAll(io.LimitReader(rc, 1<<20))
			rc.Close()
			if err != nil {
				return nil, err
			}
			name, err := cueDataFile(cue)
			if err != nil {
				return nil, err
			}
			want := path.Join(path.Dir(f.Name), strings.ReplaceAll(name, `\`, "/"))
			for _, g := range z.File {
				if strings.EqualFold(g.Name, want) {
					pick = g
				}
			}
			if pick == nil {
				return nil, fmt.Errorf("the zip has no %s (named by %s)", name, f.Name)
			}
			break
		}
	}
	if pick == nil {
		for _, f := range z.File {
			switch strings.ToLower(path.Ext(f.Name)) {
			case ".iso", ".bin", ".img", ".cso", ".zso":
				if pick == nil || f.UncompressedSize64 > pick.UncompressedSize64 {
					pick = f
				}
			}
		}
	}
	if pick == nil {
		return nil, errors.New("the zip holds no disc image (.iso, .bin/.cue, .cso, .zso)")
	}
	if pick.Method == zip.Store {
		off, err := pick.DataOffset()
		if err != nil {
			return nil, err
		}
		return container(io.NewSectionReader(r, off, int64(pick.UncompressedSize64)), int64(pick.UncompressedSize64), c)
	}
	// deflated: no random access, so extract it next to the system's temp files
	rc, err := pick.Open()
	if err != nil {
		return nil, err
	}
	defer rc.Close()
	t, err := os.CreateTemp("", "mvc2-disc-*")
	if err != nil {
		return nil, err
	}
	c.close = append(c.close, func() error { t.Close(); return os.Remove(t.Name()) })
	fmt.Fprintf(os.Stderr, "Extracting %s from the zip...\n", path.Base(pick.Name))
	if _, err := io.Copy(t, rc); err != nil {
		return nil, fmt.Errorf("extracting %s: %v", pick.Name, err)
	}
	return container(t, int64(pick.UncompressedSize64), c)
}

// ---- CSO / ZSO ----

type cso struct {
	r            io.ReaderAt
	total        int64
	block        int64
	ver          byte
	zso          bool
	align        uint
	index        []uint32
	mu           sync.Mutex
	cached       int64
	buf, scratch []byte
}

func newCSO(r io.ReaderAt) (*cso, error) {
	h := make([]byte, 24)
	if _, err := r.ReadAt(h, 0); err != nil {
		return nil, err
	}
	z := &cso{r: r, total: int64(binary.LittleEndian.Uint64(h[8:])), block: int64(binary.LittleEndian.Uint32(h[16:])),
		ver: h[20], align: uint(h[21]), zso: string(h[:4]) == "ZISO", cached: -1}
	if z.block <= 0 || z.block > 1<<20 || z.total <= 0 {
		return nil, errors.New("damaged CSO/ZSO header")
	}
	n := (z.total+z.block-1)/z.block + 1
	raw := make([]byte, 4*n)
	if _, err := r.ReadAt(raw, 24); err != nil {
		return nil, fmt.Errorf("CSO/ZSO index: %v", err)
	}
	z.index = make([]uint32, n)
	for i := range z.index {
		z.index[i] = binary.LittleEndian.Uint32(raw[4*i:])
	}
	z.buf = make([]byte, z.block)
	return z, nil
}

func (z *cso) load(b int64) error {
	if b == z.cached {
		return nil
	}
	z.cached = -1
	e0, e1 := z.index[b], z.index[b+1]
	pos := int64(e0&0x7fffffff) << z.align
	end := int64(e1&0x7fffffff) << z.align
	out := z.block
	if rest := z.total - b*z.block; rest < out {
		out = rest
	}
	if end < pos {
		return errors.New("damaged CSO/ZSO index")
	}
	n := end - pos
	flag := e0&0x80000000 != 0
	plain, lz4 := flag, z.zso
	if !z.zso && z.ver >= 2 { // CISO v2: full-size block = plain, flag = LZ4
		plain, lz4 = n >= z.block, flag
	}
	if plain {
		if _, err := z.r.ReadAt(z.buf[:out], pos); err != nil && err != io.EOF {
			return err
		}
	} else {
		if int64(cap(z.scratch)) < n {
			z.scratch = make([]byte, n)
		}
		in := z.scratch[:n]
		if m, err := z.r.ReadAt(in, pos); err != nil && !(err == io.EOF && m > 0) {
			return err
		} else {
			in = in[:m]
		}
		if lz4 {
			if m, err := lz4Block(in, z.buf[:out]); err != nil || int64(m) != out {
				return fmt.Errorf("damaged ZSO block %d", b)
			}
		} else {
			fr := flate.NewReader(bytes.NewReader(in))
			_, err := io.ReadFull(fr, z.buf[:out])
			fr.Close()
			if err != nil {
				return fmt.Errorf("damaged CSO block %d", b)
			}
		}
	}
	z.cached = b
	return nil
}

func (z *cso) ReadAt(p []byte, off int64) (int, error) {
	z.mu.Lock()
	defer z.mu.Unlock()
	done := 0
	for done < len(p) {
		if off >= z.total {
			return done, io.EOF
		}
		b := off / z.block
		if err := z.load(b); err != nil {
			return done, err
		}
		in := off - b*z.block
		k := copy(p[done:], z.buf[in:min(z.block, z.total-b*z.block)])
		done += k
		off += int64(k)
	}
	return done, nil
}

// lz4Block decodes one LZ4 block (no frame) into dst; returns bytes written.
func lz4Block(src, dst []byte) (int, error) {
	bad := errors.New("bad LZ4 block")
	si, di := 0, 0
	length := func(n int) (int, error) {
		if n != 15 {
			return n, nil
		}
		for {
			if si >= len(src) {
				return 0, bad
			}
			b := src[si]
			si++
			n += int(b)
			if b != 255 {
				return n, nil
			}
		}
	}
	for si < len(src) {
		tok := src[si]
		si++
		lit, err := length(int(tok >> 4))
		if err != nil || si+lit > len(src) || di+lit > len(dst) {
			return di, bad
		}
		di += copy(dst[di:], src[si:si+lit])
		si += lit
		if di == len(dst) || si >= len(src) {
			return di, nil // the last sequence has literals only
		}
		if si+2 > len(src) {
			return di, bad
		}
		off := int(src[si]) | int(src[si+1])<<8
		si += 2
		ml, err := length(int(tok & 15))
		ml += 4
		if err != nil || off == 0 || off > di || di+ml > len(dst) {
			return di, bad
		}
		for k := 0; k < ml; k++ {
			dst[di] = dst[di-off]
			di++
		}
	}
	return di, nil
}
