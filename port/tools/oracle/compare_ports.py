#!/usr/bin/env python3
"""Compare two port runs word by word (e.g. PSP against wasm), using only
upstream's dump outputs.

  compare_ports.py A.bin B.bin [--snap A_SNAP B_SNAP]... [--host-range LO:HI]
                   [--ignore FILE] [--show N]

A.bin / B.bin: per-frame dumps (MVC2_STATE_DUMP).  Each record holds the six
player records and the game block (0x004F20B0..0x004F4500) after fix() and
as stored; compared every frame.
--snap: full snapshots of the same frame from both runs (MVC2_STATE_SNAP=
F:PATH writes PATH after fix() and PATH.raw as stored), EE 0x004C2580 up to
0x01F00000 (BSS and heap); up to 8 per run.

compare.py is built for port vs oracle; between two ports its block hashes
also flag words neither side can translate (function pointers, host malloc
blocks) and integers that one side's fix() mistakes for a host address.
Here a word differs only when both its fixed and its raw values differ, and
is then classed as:
  host pointer   A's raw value lies in --host-range (A's host memory;
                 default the PSP user RAM 0x08800000:0x0C000000), also with
                 EE segment bits ORed on top (0x20000000/0x30000000 uncached
                 aliases the game uses for DMA)
  ignored        listed in --ignore ('0xADDR [len]  # why')
  DIVERGENCE     anything else
Exit status 1 if any DIVERGENCE word is found.
"""
import argparse
import array
import os
import sys

REC = 27616                       # record bytes (no MVC2_DUMP_EXTRA)
REG_LO, REG_HI = 0x004F20B0, 0x004F4500
REG_N = REG_HI - REG_LO
REG_OFF = 28                      # header: magic, frame, pads, state, seed, rng
SNAP_LO = 0x004C2580


def load_ignore(path):
    ign = set()
    if path:
        for ln in open(path):
            f = ln.split('#')[0].split()
            if f:
                a = int(f[0], 0)
                n = int(f[1], 0) if len(f) > 1 else 4
                ign.update(range(a & ~3, a + n, 4))
    return ign


class Tally:
    def __init__(self, hlo, hhi, ign):
        self.hlo, self.hhi, self.ign = hlo, hhi, ign
        self.ptr, self.seg, self.ignored, self.div = set(), set(), set(), {}

    def words(self, base, fa, fb, ra, rb, where):
        if fa == fb:
            return
        for i in range(len(fa)):
            if fa[i] == fb[i] or ra[i] == rb[i]:
                continue
            ad = base + 4 * i
            if self.hlo <= ra[i] < self.hhi:
                self.ptr.add(ad)
            elif ra[i] >> 28 in (1, 2, 3) and self.hlo <= (ra[i] & 0x0FFFFFFF) < self.hhi:
                self.seg.add(ad)   # host pointer | EE segment bits (uncached 0x2/0x3...)
            elif ad in self.ign:
                self.ignored.add(ad)
            elif ad not in self.div:
                self.div[ad] = (where, ra[i], rb[i])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--snap', nargs=2, action='append', default=[], metavar=('A_SNAP', 'B_SNAP'))
    ap.add_argument('--host-range', default='0x08800000:0x0C000000')
    ap.add_argument('--ignore')
    ap.add_argument('--show', type=int, default=8)
    o = ap.parse_args()
    hlo, hhi = (int(x, 0) for x in o.host_range.split(':'))
    t = Tally(hlo, hhi, load_ignore(o.ignore))

    frames = 0
    with open(o.a, 'rb') as fa, open(o.b, 'rb') as fb:
        while True:
            ra, rb = fa.read(REC), fb.read(REC)
            if len(ra) < REC or len(rb) < REC:
                break
            fr = int.from_bytes(ra[4:8], 'little')
            def reg(r, k):
                return array.array('I', r[REG_OFF + k * REG_N:REG_OFF + (k + 1) * REG_N])
            t.words(REG_LO, reg(ra, 0), reg(rb, 0), reg(ra, 1), reg(rb, 1), f'frame {fr}')
            frames += 1
    print(f'{frames} frames: player records + game block compared')

    for pa, pb in o.snap:
        def words(p):
            a = array.array('I')
            with open(p, 'rb') as f:
                a.frombytes(f.read())
            return a
        xa, xb, wa, wb = words(pa), words(pb), words(pa + '.raw'), words(pb + '.raw')
        n = min(len(xa), len(xb), len(wa), len(wb))
        t.words(SNAP_LO, xa[:n], xb[:n], wa[:n], wb[:n], 'snap ' + os.path.basename(pa))
        print(f'snapshot {os.path.basename(pa)}: {n * 4 // 1024} KB compared')

    print(f'{len(t.ptr)} words differ only as host pointers (A raw in {o.host_range})')
    print(f'{len(t.seg)} words differ as host pointers with EE segment bits (0x1/0x2/0x3 in the top nibble)')
    print(f'{len(t.ignored)} words differ in the ignore list')
    print(f'{len(t.div)} words DIVERGE')
    for ad, (where, a, b) in sorted(t.div.items())[:o.show]:
        print(f'   {where}: {ad:08X}  A {a:08X}  B {b:08X}')
    return 1 if t.div else 0


if __name__ == '__main__':
    sys.exit(main())
