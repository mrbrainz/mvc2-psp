#!/usr/bin/env python3
"""Put the game data from the player's own MvC2 PS2 disc image into the PSP
EBOOT.PBP, as its DATA.PSAR section.

Only /PS2/AFS00-03.AFS are kept (the PSP build reads nothing else), written
as a minimal ISO9660 image so the port's disc reader is unchanged. The result
holds copyrighted data: it is for the player's own use, never for git.

    psp_pack.py [--icon0 PNG] [--pic1 PNG] <your MvC2 PS2 .iso> <EBOOT.PBP in> <EBOOT.PBP out>

--icon0/--pic1 replace those sections (XMB art from tools/xmb_art.py).
"""
import struct
import sys

SECTOR = 2048
KEEP = ["AFS00.AFS", "AFS01.AFS", "AFS02.AFS", "AFS03.AFS"]


def die(msg):
    sys.exit("psp_pack: " + msg)


def iso_files(f):
    """{path: (lba, size)} for every file in an ISO9660 image."""
    def sec(lba, n=1):
        f.seek(lba * SECTOR)
        return f.read(n * SECTOR)

    pvd = sec(16)
    if pvd[1:6] != b"CD001":
        die("not an ISO9660 disc image")
    out = {}

    def walk(lba, size, prefix, depth):
        if depth > 8:
            return
        d = sec(lba, (size + SECTOR - 1) // SECTOR)
        i = 0
        while i < len(d):
            n = d[i]
            if n == 0:
                i = (i // SECTOR + 1) * SECTOR
                continue
            ext, sz = struct.unpack_from("<I", d, i + 2)[0], struct.unpack_from("<I", d, i + 10)[0]
            flags, nl = d[i + 25], d[i + 32]
            name = d[i + 33:i + 33 + nl]
            i += n
            if name in (b"\0", b"\1"):
                continue
            path = prefix + "/" + name.decode("ascii", "replace").split(";")[0]
            if flags & 2:
                walk(ext, sz, path, depth + 1)
            else:
                out[path.upper()] = (ext, sz)

    root = pvd[156:190]
    walk(struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0], "", 0)
    return out


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def dirrec(lba, size, name, is_dir):
    nb = name if isinstance(name, bytes) else name.encode("ascii")
    ln = 33 + len(nb)
    ln += ln & 1
    r = bytes([ln, 0]) + both32(lba) + both32(size) + bytes(7) + bytes([2 if is_dir else 0, 0, 0]) + both16(1)
    r += bytes([len(nb)]) + nb
    return r + bytes(ln - len(r))


def mini_iso(files):
    """files: [(name, size, reader)] under /PS2. Yields the image in chunks."""
    root_lba, ps2_lba, first = 18, 19, 20
    lbas, lba = [], first
    for _, size, _ in files:
        lbas.append(lba)
        lba += (size + SECTOR - 1) // SECTOR
    total = lba
    root = dirrec(root_lba, SECTOR, b"\0", True) + dirrec(root_lba, SECTOR, b"\1", True) + dirrec(ps2_lba, SECTOR, "PS2", True)
    ps2 = dirrec(ps2_lba, SECTOR, b"\0", True) + dirrec(root_lba, SECTOR, b"\1", True)
    for (name, size, _), l in zip(files, lbas):
        ps2 += dirrec(l, size, name + ";1", False)
    if len(ps2) > SECTOR:
        die("directory too large")
    pvd = bytearray(SECTOR)
    pvd[0:8] = b"\x01CD001\x01\x00"
    pvd[40:72] = b"MVC2_PSP".ljust(32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SECTOR)
    pvd[156:156 + 34] = dirrec(root_lba, SECTOR, b"\0", True)
    pvd[881] = 1
    term = bytearray(SECTOR)
    term[0:7] = b"\xffCD001\x01"
    yield bytes(16 * SECTOR) + bytes(pvd) + bytes(term)
    yield root.ljust(SECTOR, b"\0") + ps2.ljust(SECTOR, b"\0")
    for name, size, reader in files:
        left = size
        while left:
            b = reader(min(left, 1 << 20))
            if not b:
                die("disc image ends early in " + name)
            left -= len(b)
            yield b
        if size % SECTOR:
            yield bytes(SECTOR - size % SECTOR)


def main():
    args, art = sys.argv[1:], {}
    while args and args[0] in ("--icon0", "--pic1") and len(args) > 1:
        art[1 if args[0] == "--icon0" else 4] = open(args[1], "rb").read()
        args = args[2:]
    if len(args) != 3:
        die("usage: psp_pack.py [--icon0 PNG] [--pic1 PNG] <your MvC2 PS2 .iso> <EBOOT.PBP in> <EBOOT.PBP out>")
    iso_path, pbp_in, pbp_out = args
    with open(pbp_in, "rb") as f:
        pbp = f.read()
    if pbp[:4] != b"\0PBP":
        die(pbp_in + " is not an EBOOT.PBP")
    offs = list(struct.unpack_from("<8I", pbp, 8))
    if art:                                   # rebuild sections 0-6 with the new art
        secs = [pbp[offs[i]:offs[i + 1]] for i in range(7)]
        for i, b in art.items():
            secs[i] = b
        pbp = pbp[:8] + bytes(32)
        for i in range(7):
            offs[i] = len(pbp)
            pbp += secs[i]
        offs[7] = len(pbp)
    pbp = pbp[:offs[7]]                       # drop any DATA.PSAR already packed
    pad = (-len(pbp)) % SECTOR                # data at a sector boundary
    pbp += bytes(pad)
    offs[7] = len(pbp)
    pbp = pbp[:8] + struct.pack("<8I", *offs) + pbp[40:]

    with open(iso_path, "rb") as iso:
        found = iso_files(iso)
        if "/SLUS_204.86" not in found:
            die(iso_path + " is not Marvel vs. Capcom 2 for PS2 (SLUS-20486)")
        files = []
        for name in KEEP:
            key = "/PS2/" + name
            if key not in found:
                die(iso_path + ": no " + key)
            lba, size = found[key]
            files.append((name, size, lba))
        total = 0
        with open(pbp_out, "wb") as out:
            out.write(pbp)
            entries = [(n, s, _Lazy(iso, l)) for n, s, l in files]
            for chunk in mini_iso(entries):
                out.write(chunk)
                total += len(chunk)
    print("psp_pack: %s (%d MB of game data)" % (pbp_out, total >> 20))


class _Lazy:
    """Reads one file of the source image; seeks on the first read."""
    def __init__(self, f, lba):
        self.f, self.pos = f, lba * SECTOR

    def __call__(self, n):
        self.f.seek(self.pos)
        b = self.f.read(n)
        self.pos += len(b)
        return b


if __name__ == "__main__":
    main()
