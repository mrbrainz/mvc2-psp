#!/bin/sh
# Release packages from a checked base EBOOT (MVC2_RELEASE build).
#   port/psp/release/package.sh VERSION BASE_EBOOT OUTDIR
# Writes OUTDIR/mvc2-psp-VERSION-{windows.zip,macos.zip,linux-x64.tar.gz,pypack.zip}
# and OUTDIR/SHA256SUMS. Needs go; the macOS package needs lipo, codesign, ditto.
set -eu
[ $# -eq 3 ] || { echo "usage: $0 VERSION BASE_EBOOT OUTDIR" >&2; exit 2; }
VER=$1; BASE=$2; OUT=$3
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
REL=$ROOT/port/psp/release
NAME=mvc2-psp-$VER
python3 "$ROOT/port/psp/tools/check_release_eboot.py" "$BASE"
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT

# LICENSE.txt: third-party notices for code in the EBOOT and tools
{ echo "MvC2 PSP: third-party notices"; echo
  for f in "$ROOT"/LICENSES/README.md "$ROOT"/LICENSES/*.txt; do
    echo "==== $(basename "$f") ===="; cat "$f"; echo; done
  echo "==== PSPSDK, newlib (pspdev) and Go runtime ===="
  echo "Linked parts of PSPSDK and newlib (pspdev) and the Go standard library"
  echo "are BSD-style licensed; see https://github.com/pspdev and https://go.dev/LICENSE."
} > "$W/LICENSE.txt"

gob() { (cd "$ROOT/port/psp/builder" && CGO_ENABLED=0 GOOS=$1 GOARCH=$2 go build -trimpath -ldflags "-s -w" -o "$3" .); }
dist() { d=$W/$1/$NAME; mkdir -p "$d"; cp "$REL/README.txt" "$W/LICENSE.txt" "$d/"; echo "$d"; }

d=$(dist win); mkdir "$d/base"; cp "$BASE" "$d/base/EBOOT.PBP"
gob windows amd64 "$d/MvC2 EBOOT Builder.exe"
(cd "$W/win" && rm -f "$OUT/$NAME-windows.zip" && zip -qr -X "$OUT/$NAME-windows.zip" "$NAME")

d=$(dist linux); mkdir "$d/base"; cp "$BASE" "$d/base/EBOOT.PBP"
gob linux amd64 "$d/mvc2-eboot-builder"
tar -C "$W/linux" -czf "$OUT/$NAME-linux-x64.tar.gz" "$NAME"

d=$(dist mac); app="$d/MvC2 EBOOT Builder.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
gob darwin arm64 "$W/b-arm64"; gob darwin amd64 "$W/b-amd64"
lipo -create -output "$app/Contents/MacOS/mvc2-eboot-builder" "$W/b-arm64" "$W/b-amd64"
cp "$BASE" "$app/Contents/Resources/EBOOT.PBP"
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>MvC2 EBOOT Builder</string>
<key>CFBundleIdentifier</key><string>io.github.mvc2-psp.builder</string>
<key>CFBundleExecutable</key><string>mvc2-eboot-builder</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$VER</string>
<key>LSMinimumSystemVersion</key><string>11.0</string>
</dict></plist>
PLIST
codesign --force --deep -s - "$app"   # ad-hoc: arm64 refuses unsigned code
(cd "$W/mac" && rm -f "$OUT/$NAME-macos.zip" && ditto -c -k --keepParent "$NAME" "$OUT/$NAME-macos.zip")

d=$(dist py); mkdir -p "$d/base" "$d/port/psp/pack" "$d/port/psp/tools" "$d/port/tools/convert"
cp "$BASE" "$d/base/EBOOT.PBP"
cp "$ROOT/port/psp/pack/psp_pack.py" "$d/port/psp/pack/"
cp "$ROOT/port/psp/tools/xmb_art.py" "$d/port/psp/tools/"
cp "$ROOT/port/tools/mvc2_lz16.py" "$d/port/tools/"
for f in iso afs png tim2 textures; do cp "$ROOT/port/tools/convert/$f.py" "$d/port/tools/convert/"; done
cat >> "$d/README.txt" <<'TXT'

Python 3 instead of the builder (any OS):

  python3 port/psp/tools/xmb_art.py game.iso art
  mkdir MVC2
  python3 port/psp/pack/psp_pack.py --icon0 art/ICON0.PNG --pic1 art/PIC1.PNG game.iso base/EBOOT.PBP MVC2/EBOOT.PBP
TXT
(cd "$W/py" && rm -f "$OUT/$NAME-pypack.zip" && zip -qr -X "$OUT/$NAME-pypack.zip" "$NAME")

(cd "$OUT" && shasum -a 256 "$NAME"-* > SHA256SUMS && cat SHA256SUMS)
