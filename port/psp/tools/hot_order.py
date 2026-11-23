#!/usr/bin/env python3
"""P101: hot-routine ordering for the PSP link (ld --section-ordering-file).

    hot_order.py ELF HOT.TXT [HOT.TXT ...] -o port/psp/hot_order.ld [--budget KB]

ELF is the -DMVC2_PSP_HOTPROF=ON build's mvc2_psp (before prxgen), HOT.TXT
the hot.txt files its runs wrote (psp_hot.c).  Functions are ranked by calls
(summed over the runs) and taken until --budget KB of code; the output puts
their .text.<name> sections first in .text, hottest first (one that does not
fit is passed over for smaller ones).
"""
import argparse, os, subprocess, sys


def symbols(elf):
    nm = os.path.join(os.environ.get("PSPDEV", os.path.expanduser("~/Sites/pspdev")), "bin", "psp-nm")
    out = subprocess.run([nm, "-S", "--defined-only", elf], capture_output=True, text=True, check=True).stdout
    by_addr = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4 and p[2] in "tT":
            by_addr.setdefault(int(p[0], 16), (p[3], int(p[1], 16)))
    return by_addr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("hot", nargs="+")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--budget", type=int, default=96, help="KB of code to place first")
    a = ap.parse_args()
    syms = symbols(a.elf)
    ref_link = next((ad for ad, (n, _) in syms.items() if n == "psp_hot_dump"), None)
    if ref_link is None:
        sys.exit("psp_hot_dump not in the ELF: not a MVC2_PSP_HOTPROF build")
    calls = {}
    for path in a.hot:
        with open(path) as f:
            ref = int(f.readline().split()[1], 16)
            for line in f:
                ad, n = line.split()
                calls[int(ad, 16) - ref + ref_link] = calls.get(int(ad, 16) - ref + ref_link, 0) + int(n)
    ranked, miss = [], 0
    for ad, n in sorted(calls.items(), key=lambda kv: -kv[1]):
        if ad not in syms:
            miss += 1
            continue
        name, size = syms[ad]
        if name.startswith("__cyg_profile") or name == "psp_hot_dump":
            continue
        ranked.append((name, size, n))
    out, used, seen = [], 0, set()
    for name, size, n in ranked:
        if name in seen or used + size > a.budget * 1024:
            continue
        seen.add(name)
        out.append(name)
        used += size
    with open(a.out, "w") as f:
        f.write("/* P101: hottest routines first in .text (port/psp/tools/hot_order.py,\n"
                " * call counts of a MVC2_PSP_HOTPROF build; %d functions, %d KB) */\n" % (len(out), used // 1024))
        f.write(".text : {\n")
        for name in out:
            f.write("  *(.text.%s)\n" % name)
        f.write("}\n")
    print("%d functions, %d KB placed (of %d counted, %d unmapped)" % (len(out), used // 1024, len(calls), miss))


if __name__ == "__main__":
    main()
