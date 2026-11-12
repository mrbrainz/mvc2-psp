package main

// XMB art: a Go copy of port/psp/tools/xmb_art.py. The float maths follows
// the Python step by step (float64(...) around products stops FMA fusion) so
// the pixels match.

import (
	"bytes"
	"image"
	"image/png"
	"math"
)

type img struct {
	w, h int
	px   [][4]float64 // premultiplied r, g, b; a in 0..255
}

func newImg(w, h int) *img { return &img{w, h, make([][4]float64, w*h)} }

func fromRGBA(w, h int, rgba []byte) *img {
	m := newImg(w, h)
	for i := range m.px {
		a := float64(rgba[4*i+3]) / 255.0
		m.px[i] = [4]float64{float64(float64(rgba[4*i]) * a), float64(float64(rgba[4*i+1]) * a),
			float64(float64(rgba[4*i+2]) * a), float64(a * 255.0)}
	}
	return m
}

func (m *img) crop(x, y, w, h int) *img {
	o := newImg(w, h)
	for j := 0; j < h; j++ {
		copy(o.px[j*w:(j+1)*w], m.px[(y+j)*m.w+x:])
	}
	return o
}

func (m *img) resize(w, h int) *img { return m.axis(w, true).axis(h, false) }

type tap struct {
	k  int
	wt float64
}

// makeTaps: per output pixel, the source pixels and normalised weights.
func makeTaps(src, n int) [][]tap {
	scale := float64(src) / float64(n)
	sup := math.Max(scale, 1.0)
	taps := make([][]tap, n)
	for o := 0; o < n; o++ {
		c := float64((float64(o) + 0.5) * scale)
		lo, hi := int(c-sup), int(c+sup)+1
		if lo < 0 {
			lo = 0
		}
		if hi > src {
			hi = src
		}
		var ws []tap
		var sum pySum
		for k := lo; k < hi; k++ {
			wt := math.Max(0.0, 1-math.Abs((float64(k)+0.5-c)/sup))
			ws = append(ws, tap{k, wt})
			sum.add(wt)
		}
		t := sum.result()
		if t == 0 {
			t = 1.0
		}
		for _, x := range ws {
			if x.wt != 0 {
				taps[o] = append(taps[o], tap{x.k, x.wt / t})
			}
		}
	}
	return taps
}

func (m *img) axis(n int, horiz bool) *img {
	src := m.h
	if horiz {
		src = m.w
	}
	taps := makeTaps(src, n)
	ow, oh := m.w, n
	if horiz {
		ow, oh = n, m.h
	}
	out := newImg(ow, oh)
	for y := 0; y < oh; y++ {
		for x := 0; x < ow; x++ {
			acc := &out.px[y*ow+x]
			idx := y
			if horiz {
				idx = x
			}
			for _, tp := range taps[idx] {
				var s [4]float64
				if horiz {
					s = m.px[y*m.w+tp.k]
				} else {
					s = m.px[tp.k*m.w+x]
				}
				for c := 0; c < 4; c++ {
					acc[c] += float64(s[c] * tp.wt)
				}
			}
		}
	}
	return out
}

// pySum is CPython 3.12+ sum() over floats (Neumaier compensated).
type pySum struct{ f, c float64 }

func (s *pySum) add(x float64) {
	t := s.f + x
	if math.Abs(s.f) >= math.Abs(x) {
		s.c += (s.f - t) + x
	} else {
		s.c += (x - t) + s.f
	}
	s.f = t
}

func (s *pySum) result() float64 {
	if s.c != 0 && !math.IsInf(s.c, 0) && !math.IsNaN(s.c) {
		return s.f + s.c
	}
	return s.f
}

func (m *img) over(top *img, x0, y0 int) {
	for j := 0; j < top.h; j++ {
		for i := 0; i < top.w; i++ {
			s, d := top.px[j*top.w+i], &m.px[(y0+j)*m.w+x0+i]
			k := 1 - s[3]/255.0
			for c := 0; c < 4; c++ {
				d[c] = s[c] + float64(d[c]*k)
			}
		}
	}
}

func clamp8(v float64) uint8 {
	r := math.RoundToEven(v)
	if r < 0 {
		return 0
	}
	if r > 255 {
		return 255
	}
	return uint8(r)
}

func (m *img) png() []byte {
	o := image.NewNRGBA(image.Rect(0, 0, m.w, m.h))
	for i, p := range m.px {
		f := 0.0
		if p[3] > 0 {
			f = 255.0 / p[3]
		}
		o.Pix[4*i] = clamp8(float64(p[0] * f))
		o.Pix[4*i+1] = clamp8(float64(p[1] * f))
		o.Pix[4*i+2] = clamp8(float64(p[2] * f))
		o.Pix[4*i+3] = clamp8(p[3])
	}
	var b bytes.Buffer
	png.Encode(&b, o)
	return b.Bytes()
}

// xmbArt returns ICON0.PNG (144x80) and PIC1.PNG (480x272) from AFS02.
func xmbArt(a *afs) (icon0, pic1 []byte, err error) {
	raw, err := a.find("DM13TEX.BIN")
	if err != nil {
		return nil, nil, err
	}
	bg, err := packPic(raw, 1)
	if err != nil {
		return nil, nil, err
	}
	icon := bg.resize(144, 144).crop(0, 32, 144, 80)
	if raw, err = a.find("DM0ATEX.BIN"); err != nil {
		return nil, nil, err
	}
	logo, err := packPic(raw, 1)
	if err != nil {
		return nil, nil, err
	}
	logo = logo.crop(0, 0, logo.w, 77).resize(133, 80)
	icon.over(logo, (144-133)/2, 0)

	if raw, err = a.find("GAL39.TM2"); err != nil {
		return nil, nil, err
	}
	gal, err := tim2(raw, 0)
	if err != nil {
		return nil, nil, err
	}
	first, last := -1, -1
	for y := 0; y < gal.h; y++ {
		for x := 0; x < gal.w; x++ {
			if gal.px[y*gal.w+x][3] > 0 {
				if first < 0 {
					first = y
				}
				last = y
				break
			}
		}
	}
	if first < 0 {
		return nil, nil, errBadData
	}
	gal = gal.crop(0, first, gal.w, last-first+1)
	pic := gal.resize(480, 360).crop(0, 0, 480, 272)
	return icon.png(), pic.png(), nil
}
