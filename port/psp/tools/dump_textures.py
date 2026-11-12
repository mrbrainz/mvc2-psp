#!/usr/bin/env python3
"""Dump every texture in the user's ISO to PNG, for picking XMB art.

  dump_textures.py ISO [OUTDIR]     (default OUTDIR: private/dump)

Walks all AFS02 entries through upstream port/tools/convert/textures.py
(TIM2 files and TEX packs); other entries are skipped. Writes index.txt
listing every converted entry. OUTDIR must stay out of git.
"""
import contextlib, io, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'port', 'tools', 'convert'))
from iso import Iso
from afs import Afs
import textures


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    outdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'private', 'dump')
    os.makedirs(outdir, exist_ok=True)
    a = Afs(Iso(sys.argv[1]), 2)
    index, ok = [], 0
    for i in range(len(a)):
        raw = a.read(i)
        if raw[:4] != b'TIM2':
            try:
                d, _ = textures.decode_pack(raw)
            except Exception:
                continue
            if not (textures.records(d) or textures.table12(d)):
                continue
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                textures.convert_entry(a, i, outdir)
        except Exception as e:
            buf.write('not converted (%s: %s)\n' % (type(e).__name__, e))
        ok += 1
        index.append('== %03d %s\n%s' % (i, a.name(i), buf.getvalue()))
    open(os.path.join(outdir, 'index.txt'), 'w').write('\n'.join(index))
    pngs = sum(f.endswith('.png') for _, _, fs in os.walk(outdir) for f in fs)
    print('%d texture entries of %d, %d PNGs -> %s' % (ok, len(a), pngs, outdir))


if __name__ == '__main__':
    main()
