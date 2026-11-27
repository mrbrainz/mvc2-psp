#!/usr/bin/env python3
"""Put the game data from the player's own MvC2 PS2 disc image into the PSP
EBOOT.PBP, as its DATA.PSAR section.

Only /SLUS_204.86 (the EE data the port fills from the executable) and
/PS2/AFS00-03.AFS are kept (the PSP build reads nothing else), written
as a minimal ISO9660 image so the port's disc reader is unchanged. The result
holds copyrighted data: it is for the player's own use, never for git.

    psp_pack.py [--icon0 PNG] [--pic1 PNG] <your MvC2 PS2 disc image> <EBOOT.PBP in> <EBOOT.PBP out>

The disc image may be an .iso, a raw .bin (2352-byte sectors), a .cue, a
CSO/ZSO, or a .zip holding one of these (same as the builder, port/psp/builder).
--icon0/--pic1 replace those sections (XMB art from tools/xmb_art.py).
"""
import atexit
import os
import posixpath
import shutil
import struct
import sys
import tempfile
import zipfile
import zlib

SECTOR = 2048
KEEP = ["/SLUS_204.86", "/PS2/AFS00.AFS", "/PS2/AFS01.AFS", "/PS2/AFS02.AFS", "/PS2/AFS03.AFS"]


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
    """files: [(path, size, reader)], path "/NAME" or "/PS2/NAME". Yields the image in chunks."""
    root_lba, ps2_lba, first = 18, 19, 20
    lbas, lba = [], first
    for _, size, _ in files:
        lbas.append(lba)
        lba += (size + SECTOR - 1) // SECTOR
    total = lba
    root = dirrec(root_lba, SECTOR, b"\0", True) + dirrec(root_lba, SECTOR, b"\1", True) + dirrec(ps2_lba, SECTOR, "PS2", True)
    ps2 = dirrec(ps2_lba, SECTOR, b"\0", True) + dirrec(root_lba, SECTOR, b"\1", True)
    for (path, size, _), l in zip(files, lbas):
        rec = dirrec(l, size, path.rsplit("/", 1)[1] + ";1", False)
        if path.startswith("/PS2/"):
            ps2 += rec
        else:
            root += rec
    if len(ps2) > SECTOR or len(root) > SECTOR:
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
        die("usage: psp_pack.py [--icon0 PNG] [--pic1 PNG] <your MvC2 PS2 disc image> <EBOOT.PBP in> <EBOOT.PBP out>")
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

    with open_image(iso_path) as iso:
        found = iso_files(iso)
        if "/SLUS_204.86" not in found:
            die(iso_path + " is not Marvel vs. Capcom 2 for PS2 (SLUS-20486)")
        files = []
        for key in KEEP:
            if key not in found:
                die(iso_path + ": no " + key)
            lba, size = found[key]
            files.append((key, size, lba))
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


# ---- disc image containers, read as a 2048-byte-sector view ----

class _View:
    """seek/read over read_at(off, n)."""
    pos = 0

    def seek(self, p):
        self.pos = p

    def read(self, n):
        b = self.read_at(self.pos, n)
        self.pos += len(b)
        return b

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


class _File(_View):
    def __init__(self, f, base=0, size=None, cleanup=None):
        self.f, self.base, self.cleanup = f, base, cleanup
        self.size = size if size is not None else os.fstat(f.fileno()).st_size - base

    def read_at(self, off, n):
        n = max(0, min(n, self.size - off))
        self.f.seek(self.base + off)
        return self.f.read(n)

    def close(self):
        self.f.close()
        if self.cleanup:
            self.cleanup()


class _Raw(_View):
    """2352-byte sectors, user data at +hdr."""
    def __init__(self, src, hdr):
        self.src, self.hdr = src, hdr

    def read_at(self, off, n):
        out = []
        while n > 0:
            lba, inn = divmod(off, SECTOR)
            k = min(n, SECTOR - inn)
            b = self.src.read_at(lba * 2352 + self.hdr + inn, k)
            out.append(b)
            if len(b) < k:
                break
            off, n = off + k, n - k
        return b"".join(out)

    def close(self):
        self.src.close()


def _lz4_block(src, size):
    out, si = bytearray(), 0

    def length(n):
        nonlocal si
        if n == 15:
            while True:
                b = src[si]
                si += 1
                n += b
                if b != 255:
                    break
        return n

    while si < len(src):
        tok = src[si]
        si += 1
        lit = length(tok >> 4)
        out += src[si:si + lit]
        si += lit
        if len(out) >= size or si >= len(src):
            break
        off = src[si] | src[si + 1] << 8
        si += 2
        ml = length(tok & 15) + 4
        if not off or off > len(out):
            die("damaged ZSO block")
        start = len(out) - off
        if ml <= off:
            out += out[start:start + ml]
        else:
            for i in range(ml):
                out.append(out[start + i])
    return bytes(out[:size])


class _Cso(_View):
    """CISO v1 (deflate), v2 (deflate/LZ4), ZISO (LZ4)."""
    def __init__(self, src):
        h = src.read_at(0, 24)
        self.src, self.zso = src, h[:4] == b"ZISO"
        self.total, self.block = struct.unpack_from("<QI", h, 8)
        self.ver, self.align = h[20], h[21]
        if not 0 < self.block <= 1 << 20:
            die("damaged CSO/ZSO header")
        n = (self.total + self.block - 1) // self.block + 1
        self.index = struct.unpack("<%dI" % n, src.read_at(24, 4 * n))
        self.cached, self.buf = -1, b""

    def load(self, b):
        if b == self.cached:
            return
        e0, e1 = self.index[b], self.index[b + 1]
        pos, end = (e0 & 0x7fffffff) << self.align, (e1 & 0x7fffffff) << self.align
        out = min(self.block, self.total - b * self.block)
        flag = bool(e0 & 0x80000000)
        plain, lz4 = flag, self.zso
        if not self.zso and self.ver >= 2:
            plain, lz4 = end - pos >= self.block, flag
        data = self.src.read_at(pos, out if plain else end - pos)
        if plain:
            self.buf = data
        elif lz4:
            self.buf = _lz4_block(data, out)
        else:
            self.buf = zlib.decompressobj(-15).decompress(data, out)
        if len(self.buf) != out:
            die("damaged CSO/ZSO block %d" % b)
        self.cached = b

    def read_at(self, off, n):
        out = []
        while n > 0 and off < self.total:
            b, inn = divmod(off, self.block)
            self.load(b)
            piece = self.buf[inn:inn + n]
            out.append(piece)
            off, n = off + len(piece), n - len(piece)
        return b"".join(out)

    def close(self):
        self.src.close()


def _cue_data_file(text):
    name = None
    for line in text.splitlines():
        f = line.split()
        if len(f) >= 2 and f[0].upper() == "FILE":
            rest = line.strip()[4:].strip()
            name = rest[1:rest.index('"', 1)] if rest.startswith('"') and '"' in rest[1:] else f[1]
        elif len(f) >= 3 and f[0].upper() == "TRACK" and f[2].upper().startswith("MODE") and name:
            return name
    die("the .cue sheet names no data track")


def _from_zip(path):
    z = zipfile.ZipFile(path)
    names = {n.lower(): n for n in z.namelist()}
    pick = None
    for n in z.namelist():
        if n.lower().endswith(".cue"):
            want = posixpath.join(posixpath.dirname(n), _cue_data_file(z.read(n).decode("latin-1")).replace("\\", "/"))
            pick = names.get(want.lower()) or die("the zip has no %s (named by %s)" % (want, n))
            break
    if not pick:
        imgs = [i for i in z.infolist() if os.path.splitext(i.filename)[1].lower() in (".iso", ".bin", ".img", ".cso", ".zso")]
        if not imgs:
            die("the zip holds no disc image (.iso, .bin/.cue, .cso, .zso)")
        pick = max(imgs, key=lambda i: i.file_size).filename
    info = z.getinfo(pick)
    if info.compress_type == zipfile.ZIP_STORED:
        f = open(path, "rb")
        f.seek(info.header_offset)
        lh = f.read(30)
        base = info.header_offset + 30 + struct.unpack_from("<H", lh, 26)[0] + struct.unpack_from("<H", lh, 28)[0]
        return _framed(_File(f, base, info.file_size))
    d = tempfile.mkdtemp(prefix="mvc2-disc-")
    atexit.register(shutil.rmtree, d, True)
    print("psp_pack: extracting %s from the zip..." % os.path.basename(pick))
    t = os.path.join(d, "disc")
    with z.open(info) as src, open(t, "wb") as dst:
        shutil.copyfileobj(src, dst, 1 << 20)
    return _framed(_File(open(t, "rb"), cleanup=lambda: shutil.rmtree(d, True)))


def _framed(src):
    head = src.read_at(0, 16)
    if head[:4] in (b"CISO", b"ZISO"):
        return _Cso(src)
    if head[:8] == b"MComprHD":
        die("that's a CHD image; convert it to an .iso first (chdman extractcd)")
    if head[:4] in (b"7z\xbc\xaf", b"Rar!"):
        die("that's a 7z/RAR archive; extract the disc image from it (or zip it) first")
    if src.read_at(16 * SECTOR + 1, 5) == b"CD001":
        return src
    for hdr in (16, 24):
        if src.read_at(16 * 2352 + hdr + 1, 5) == b"CD001":
            return _Raw(src, hdr)
    return src


def open_image(path):
    """Any supported disc image, as a seek/read 2048-byte-sector view."""
    if path.lower().endswith(".cue"):
        with open(path, encoding="latin-1") as f:
            path = os.path.join(os.path.dirname(path), _cue_data_file(f.read()))
    with open(path, "rb") as f:
        zipped = f.read(4) == b"PK\x03\x04"
    return _from_zip(path) if zipped else _framed(_File(open(path, "rb")))


if __name__ == "__main__":
    main()
