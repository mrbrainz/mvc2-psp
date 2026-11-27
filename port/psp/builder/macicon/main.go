// Command macicon puts square app art on Apple's macOS icon grid: the art
// scaled to an 824 px rounded tile (corner radius 185.4) centred on a
// transparent 1024 px canvas, with a soft drop shadow. package.sh makes the
// .icns sizes from the result.
//
//	go run ./macicon IN.png OUT.png
package main

import (
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"image/png"
	"math"
	"os"
)

const (
	canvas = 1024
	tile   = 824
	radius = 185.4
	shY    = 10.0 // shadow: y offset, blur sigma, opacity
	shBlur = 10.0
	shA    = 0.3
)

func main() {
	if len(os.Args) != 3 {
		fmt.Fprintln(os.Stderr, "usage: macicon IN.png OUT.png")
		os.Exit(2)
	}
	f, err := os.Open(os.Args[1])
	if err != nil {
		fail(err)
	}
	src, err := png.Decode(f)
	f.Close()
	if err != nil {
		fail(err)
	}
	b := src.Bounds()
	if b.Dx() != b.Dy() {
		fail(fmt.Errorf("%s is not square (%dx%d)", os.Args[1], b.Dx(), b.Dy()))
	}
	rgba := image.NewNRGBA(image.Rect(0, 0, b.Dx(), b.Dy()))
	draw.Draw(rgba, rgba.Bounds(), src, b.Min, draw.Src)
	art := resize(rgba, tile)

	off := float64(canvas-tile) / 2
	mask := coverage(off)
	shadow := blur(shift(mask, int(shY)), shBlur)

	out := image.NewNRGBA(image.Rect(0, 0, canvas, canvas))
	for y := 0; y < canvas; y++ {
		for x := 0; x < canvas; x++ {
			i := y*canvas + x
			// art over shadow, both straight alpha
			sa := shadow[i] * shA
			var c [3]float64
			aa := 0.0
			ax, ay := x-int(off), y-int(off)
			if ax >= 0 && ay >= 0 && ax < tile && ay < tile {
				p := art.NRGBAAt(ax, ay)
				aa = mask[i] * float64(p.A) / 255
				c = [3]float64{float64(p.R), float64(p.G), float64(p.B)}
			}
			a := aa + sa*(1-aa)
			if a <= 0 {
				continue
			}
			var px color.NRGBA
			px.A = uint8(math.Round(a * 255))
			for k, v := range c {
				v = v * aa / a // shadow is black
				switch k {
				case 0:
					px.R = uint8(math.Round(v))
				case 1:
					px.G = uint8(math.Round(v))
				case 2:
					px.B = uint8(math.Round(v))
				}
			}
			out.SetNRGBA(x, y, px)
		}
	}
	w, err := os.Create(os.Args[2])
	if err != nil {
		fail(err)
	}
	if err := png.Encode(w, out); err != nil {
		fail(err)
	}
	if err := w.Close(); err != nil {
		fail(err)
	}
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "macicon:", err)
	os.Exit(1)
}

// coverage: the rounded tile's anti-aliased coverage per canvas pixel (4x4 samples).
func coverage(off float64) []float64 {
	m := make([]float64, canvas*canvas)
	inside := func(x, y float64) bool {
		x, y = x-off, y-off
		if x < 0 || y < 0 || x > tile || y > tile {
			return false
		}
		cx := math.Max(radius-x, x-(tile-radius))
		cy := math.Max(radius-y, y-(tile-radius))
		if cx <= 0 || cy <= 0 {
			return true
		}
		return cx*cx+cy*cy <= radius*radius
	}
	for y := 0; y < canvas; y++ {
		for x := 0; x < canvas; x++ {
			n := 0
			for sy := 0; sy < 4; sy++ {
				for sx := 0; sx < 4; sx++ {
					if inside(float64(x)+(float64(sx)+0.5)/4, float64(y)+(float64(sy)+0.5)/4) {
						n++
					}
				}
			}
			m[y*canvas+x] = float64(n) / 16
		}
	}
	return m
}

func shift(m []float64, dy int) []float64 {
	o := make([]float64, len(m))
	copy(o[dy*canvas:], m[:len(m)-dy*canvas])
	return o
}

// blur: Gaussian approximated by three box blurs, rows then columns.
func blur(m []float64, sigma float64) []float64 {
	r := int(math.Round(math.Sqrt(12*sigma*sigma/3+1)-1) / 2)
	for pass := 0; pass < 3; pass++ {
		m = box(m, r, 1, canvas)
		m = box(m, r, canvas, 1)
	}
	return m
}

// box averages over 2r+1 samples along one axis (step between samples, stride between lines).
func box(m []float64, r, step, stride int) []float64 {
	o := make([]float64, len(m))
	for line := 0; line < canvas; line++ {
		base := line * stride
		sum := 0.0
		at := func(i int) float64 {
			if i < 0 || i >= canvas {
				return 0
			}
			return m[base+i*step]
		}
		for i := -r; i <= r; i++ {
			sum += at(i)
		}
		for i := 0; i < canvas; i++ {
			o[base+i*step] = sum / float64(2*r+1)
			sum += at(i+r+1) - at(i-r)
		}
	}
	return o
}

// resize: area-average downscale (or bilinear-free upscale by area) to n x n,
// in premultiplied alpha.
func resize(src *image.NRGBA, n int) *image.NRGBA {
	sn := src.Bounds().Dx()
	scale := float64(sn) / float64(n)
	type acc struct{ r, g, b, a float64 }
	weights := func(d int) (int, int, []float64) {
		lo, hi := float64(d)*scale, float64(d+1)*scale
		i0, i1 := int(math.Floor(lo)), int(math.Ceil(hi))
		if i1 > sn {
			i1 = sn
		}
		w := make([]float64, i1-i0)
		for i := i0; i < i1; i++ {
			w[i-i0] = math.Min(hi, float64(i+1)) - math.Max(lo, float64(i))
		}
		return i0, i1, w
	}
	// rows first
	tmp := make([]acc, n*sn)
	for y := 0; y < sn; y++ {
		for d := 0; d < n; d++ {
			i0, _, w := weights(d)
			var s acc
			tw := 0.0
			for k, wk := range w {
				p := src.NRGBAAt(i0+k, y)
				a := float64(p.A) / 255
				s.r += wk * float64(p.R) * a
				s.g += wk * float64(p.G) * a
				s.b += wk * float64(p.B) * a
				s.a += wk * a
				tw += wk
			}
			tmp[y*n+d] = acc{s.r / tw, s.g / tw, s.b / tw, s.a / tw}
		}
	}
	out := image.NewNRGBA(image.Rect(0, 0, n, n))
	for d := 0; d < n; d++ {
		i0, _, w := weights(d)
		for x := 0; x < n; x++ {
			var s acc
			tw := 0.0
			for k, wk := range w {
				p := tmp[(i0+k)*n+x]
				s.r += wk * p.r
				s.g += wk * p.g
				s.b += wk * p.b
				s.a += wk * p.a
				tw += wk
			}
			a := s.a / tw
			px := color.NRGBA{A: uint8(math.Round(a * 255))}
			if a > 0 {
				px.R = uint8(math.Round(s.r / tw / a))
				px.G = uint8(math.Round(s.g / tw / a))
				px.B = uint8(math.Round(s.b / tw / a))
			}
			out.SetNRGBA(x, d, px)
		}
	}
	return out
}
