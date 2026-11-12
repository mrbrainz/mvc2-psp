#!/usr/bin/env python3
"""Fails unless an EBOOT.PBP carries no disc-derived data: ICON0, ICON1,
PIC0, PIC1, SND0 and DATA.PSAR all empty. Usage: check_release_eboot.py EBOOT.PBP"""
import struct, sys

NAMES = ["PARAM.SFO", "ICON0.PNG", "ICON1.PMF", "PIC0.PNG", "PIC1.PNG", "SND0.AT3", "DATA.PSP", "DATA.PSAR"]
EMPTY = {"ICON0.PNG", "ICON1.PMF", "PIC0.PNG", "PIC1.PNG", "SND0.AT3", "DATA.PSAR"}

with open(sys.argv[1], "rb") as f:
    data = f.read()
if data[:4] != b"\0PBP":
    sys.exit(sys.argv[1] + ": not an EBOOT.PBP")
offs = list(struct.unpack_from("<8I", data, 8)) + [len(data)]
bad = [n for i, n in enumerate(NAMES) if n in EMPTY and offs[i + 1] > offs[i]]
if bad:
    sys.exit("not a release EBOOT, has: " + ", ".join(bad))
print("release EBOOT OK: no art or game data")
