#!/usr/bin/env python3
"""Compare two port state dumps word by word (e.g. PSP against wasm).

  compare_ports.py A.bin B.bin [--host-range LO:HI] [--ignore FILE] [--show N]

Both dumps must be written with
  MVC2_DUMP_EXTRA=0x004C2580:0x00634380 MVC2_DUMP_EXTRA_RAW=1
(and the same MVC2_STATE_DUMP_FROM), so each record carries the whole hashed
EE BSS twice: after fix() and as stored.  compare.py is built for port vs
oracle; between two ports its block hashes also flag words that neither side
can translate (function pointers, host malloc blocks) and integers that one
side's fix() mistakes for a host address.  Here a word differs only when both
its fixed and its raw values differ, and is then classed as:
  host pointer   A's raw value lies in --host-range (A's host memory;
                 default the PSP user RAM 0x08800000:0x0C000000)
  ignored        listed in --ignore ('0xADDR [len]  # why', as compare.py)
  DIVERGENCE     anything else
Exit status 1 if any DIVERGENCE word is found.
"""
import argparse
import array
import sys

HDR = 27616                      # record bytes before the extra range
LO, HI = 0x004C2580, 0x00634380
EXTRA = HI - LO
REC = HDR + 2 * EXTRA


def load_ignore(path):
    ign = set()
    if path:
        for ln in open(path):
            f = ln.split('#')[0].split()
            if f:
                a = int(f[0], 0)
                n = int(f[1], 0) if len(f) > 1 else 4
                ign.update(range((a - LO) // 4, (a - LO + n + 3) // 4))
    return ign


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--host-range', default='0x08800000:0x0C000000')
    ap.add_argument('--ignore')
    ap.add_argument('--show', type=int, default=8)
    o = ap.parse_args()
    hlo, hhi = (int(x, 0) for x in o.host_range.split(':'))
    ign = load_ignore(o.ignore)
    fa, fb = open(o.a, 'rb'), open(o.b, 'rb')
    seen, ptrs, ignored, frames = {}, set(), set(), 0
    while True:
        ra, rb = fa.read(REC), fb.read(REC)
        if len(ra) < REC or len(rb) < REC:
            break
        fr = int.from_bytes(ra[4:8], 'little')
        xa = array.array('I', ra[HDR:HDR + EXTRA]); xb = array.array('I', rb[HDR:HDR + EXTRA])
        wa = array.array('I', ra[HDR + EXTRA:]); wb = array.array('I', rb[HDR + EXTRA:])
        if xa == xb:
            frames += 1
            continue
        for i in range(len(xa)):
            if xa[i] == xb[i] or wa[i] == wb[i]:
                continue
            if hlo <= wa[i] < hhi:
                ptrs.add(i)
            elif i in ign:
                ignored.add(i)
            elif i not in seen:
                seen[i] = (fr, wa[i], wb[i])
        frames += 1
    print(f'{frames} frames compared')
    print(f'{len(ptrs)} words differ only as host pointers (A raw in {o.host_range})')
    print(f'{len(ignored)} words differ in the ignore list')
    print(f'{len(seen)} words DIVERGE')
    for i, (fr, a, b) in sorted(seen.items(), key=lambda kv: kv[1][0])[:o.show]:
        print(f'   frame {fr}: {LO + 4 * i:08X}  A {a:08X}  B {b:08X}')
    return 1 if seen else 0


if __name__ == '__main__':
    sys.exit(main())
