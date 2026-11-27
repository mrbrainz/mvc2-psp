// Command winres writes a Windows icon resource as a COFF object (.syso)
// that the Go linker puts into the builder's .exe. Each PNG becomes one
// icon image (PNG-compressed icon entries, Windows Vista and later).
//
//	go run ./winres OUT.syso ICON256.png ICON48.png ...
package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"os"
)

const (
	rtIcon      = 3
	rtGroupIcon = 14
	langEnUS    = 0x0409
)

func main() {
	if len(os.Args) < 3 {
		fmt.Fprintln(os.Stderr, "usage: winres OUT.syso ICON.png...")
		os.Exit(2)
	}
	var icons [][]byte
	for _, p := range os.Args[2:] {
		b, err := os.ReadFile(p)
		if err != nil {
			fail(err)
		}
		if len(b) < 24 || string(b[1:4]) != "PNG" {
			fail(fmt.Errorf("%s is not a PNG", p))
		}
		icons = append(icons, b)
	}
	if err := os.WriteFile(os.Args[1], syso(icons), 0o644); err != nil {
		fail(err)
	}
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "winres:", err)
	os.Exit(1)
}

// group: the RT_GROUP_ICON entry that lists icon ids 1..n.
func group(icons [][]byte) []byte {
	var g bytes.Buffer
	le := func(v any) { binary.Write(&g, binary.LittleEndian, v) }
	le([3]uint16{0, 1, uint16(len(icons))})
	for i, b := range icons {
		w, h := binary.BigEndian.Uint32(b[16:]), binary.BigEndian.Uint32(b[20:])
		if w >= 256 {
			w = 0
		}
		if h >= 256 {
			h = 0
		}
		g.Write([]byte{byte(w), byte(h), 0, 0})
		le([2]uint16{1, 32})
		le(uint32(len(b)))
		le(uint16(i + 1))
	}
	return g.Bytes()
}

// syso lays out .rsrc: type, name and language directories, data entries,
// then the data; data entry addresses are relocated against the section.
func syso(icons [][]byte) []byte {
	type res struct {
		typ, id int
		data    []byte
	}
	var all []res
	for i, b := range icons {
		all = append(all, res{rtIcon, i + 1, b})
	}
	all = append(all, res{rtGroupIcon, 1, group(icons)})
	n := len(icons)

	dir := func(entries int) int { return 16 + 8*entries }
	rootOff := 0
	iconDirOff := rootOff + dir(2)
	groupDirOff := iconDirOff + dir(n)
	langOff := groupDirOff + dir(1) // one language directory per resource
	dataEntOff := langOff + dir(1)*len(all)
	blobOff := dataEntOff + 16*len(all)

	var s bytes.Buffer
	le := func(v any) { binary.Write(&s, binary.LittleEndian, v) }
	hdr := func(ids int) { le([4]uint32{0, 0, 0, uint32(ids) << 16}) } // chars, time, ver, 0 named + ids
	sub := uint32(0x80000000)

	hdr(2)
	le([2]uint32{rtIcon, sub | uint32(iconDirOff)})
	le([2]uint32{rtGroupIcon, sub | uint32(groupDirOff)})
	hdr(n)
	for i := 0; i < n; i++ {
		le([2]uint32{uint32(i + 1), sub | uint32(langOff+dir(1)*i)})
	}
	hdr(1)
	le([2]uint32{1, sub | uint32(langOff+dir(1)*n)})
	for i := range all {
		hdr(1)
		le([2]uint32{langEnUS, uint32(dataEntOff + 16*i)})
	}
	var relocs []uint32
	pos := blobOff
	for _, r := range all {
		relocs = append(relocs, uint32(s.Len()))
		le([4]uint32{uint32(pos), uint32(len(r.data)), 0, 0})
		pos += (len(r.data) + 7) &^ 7
	}
	for _, r := range all {
		s.Write(r.data)
		for s.Len()%8 != 0 {
			s.WriteByte(0)
		}
	}
	sec := s.Bytes()

	// COFF: header, one section header, section data, relocations, symbol table, string table
	const hdrSize, secHdrSize = 20, 40
	dataPtr := hdrSize + secHdrSize
	relPtr := dataPtr + len(sec)
	symPtr := relPtr + 10*len(relocs)
	var o bytes.Buffer
	w := func(v any) { binary.Write(&o, binary.LittleEndian, v) }
	w(uint16(0x8664)) // AMD64
	w(uint16(1))
	w(uint32(0))
	w(uint32(symPtr))
	w(uint32(1))
	w(uint16(0))
	w(uint16(0))
	o.WriteString(".rsrc\x00\x00\x00")
	w([4]uint32{0, 0, uint32(len(sec)), uint32(dataPtr)})
	w([2]uint32{uint32(relPtr), 0})
	w([2]uint16{uint16(len(relocs)), 0})
	w(uint32(0x40000040)) // initialized data, readable
	o.Write(sec)
	for _, r := range relocs {
		w(r)
		w(uint32(0)) // symbol 0: .rsrc
		w(uint16(3)) // IMAGE_REL_AMD64_ADDR32NB
	}
	o.WriteString(".rsrc\x00\x00\x00")
	w(uint32(0))   // value
	w(uint16(1))   // section 1
	w(uint16(0))   // type
	o.WriteByte(3) // static
	o.WriteByte(0) // no aux records
	w(uint32(4))   // empty string table
	return o.Bytes()
}
