#!/bin/sh
# Release packages from a checked base EBOOT (MVC2_RELEASE build).
#   port/psp/release/package.sh VERSION BASE_EBOOT OUTDIR
# Writes OUTDIR/mvc2-psp-VERSION-{windows.zip,macos.zip,linux-x64.tar.gz,linux-arm64.tar.gz,pypack.zip}
# and OUTDIR/SHA256SUMS. Needs go, and macOS for the rest: sips, iconutil (app icons
# from assets/mvc2-app-icon.png), lipo, codesign, ditto.
set -eu
[ $# -eq 3 ] || { echo "usage: $0 VERSION BASE_EBOOT OUTDIR" >&2; exit 2; }
VER=$1; BASE=$2; OUT=$3
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
REL=$ROOT/port/psp/release
NAME=mvc2-psp-$VER
python3 "$ROOT/port/psp/tools/check_release_eboot.py" "$BASE"
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
W=$(mktemp -d); SYSO=$ROOT/port/psp/builder/rsrc_windows_amd64.syso
trap 'rm -rf "$W" "$SYSO"' EXIT

# app icons: PNGs at each size, a macOS .icns, and the .exe's icon resource
I=$W/icon; mkdir -p "$I/AppIcon.iconset"
for s in 16 32 48 64 128 256 512 1024; do
  sips -s format png -z $s $s "$ROOT/assets/mvc2-app-icon.png" --out "$I/$s.png" >/dev/null; done
# macOS: the art as Apple's rounded tile with a shadow (Windows and Linux keep the square)
(cd "$ROOT/port/psp/builder" && go run ./macicon "$ROOT/assets/mvc2-app-icon.png" "$I/mac-1024.png")
for s in 16 32 64 128 256 512; do sips -z $s $s "$I/mac-1024.png" --out "$I/mac-$s.png" >/dev/null; done
for s in 16 32 128 256 512; do
  cp "$I/mac-$s.png" "$I/AppIcon.iconset/icon_${s}x$s.png"
  cp "$I/mac-$((s * 2)).png" "$I/AppIcon.iconset/icon_${s}x$s@2x.png"; done
iconutil -c icns -o "$I/AppIcon.icns" "$I/AppIcon.iconset"

# LICENSE.txt: the PSP code licence, then third-party notices for code in the EBOOT and tools
{ cat "$ROOT/LICENSE"; echo; echo "MvC2 PSP: third-party notices"; echo
  for f in "$ROOT"/LICENSES/README.md "$ROOT"/LICENSES/*.txt; do
    echo "==== $(basename "$f") ===="; cat "$f"; echo; done
  echo "==== PSPSDK, newlib (pspdev) and Go runtime ===="
  echo "Linked parts of PSPSDK and newlib (pspdev) and the Go standard library"
  echo "are BSD-style licensed; see https://github.com/pspdev and https://go.dev/LICENSE."
} > "$W/LICENSE.txt"

gob() { (cd "$ROOT/port/psp/builder" && CGO_ENABLED=0 GOOS=$1 GOARCH=$2 go build -trimpath -ldflags "-s -w" -o "$3" .); }
dist() { d=$W/$1/$NAME; mkdir -p "$d"; cp "$REL/README.txt" "$W/LICENSE.txt" "$d/"; echo "$d"; }
# README.txt's "Opening the builder" section for one package (the builders are not signed)
how() { printf '\nOpening the builder\n-------------------\n\n' >> "$1/README.txt"; cat >> "$1/README.txt"; }

d=$(dist win); mkdir "$d/base"; cp "$BASE" "$d/base/EBOOT.PBP"
(cd "$ROOT/port/psp/builder" && go run ./winres "$SYSO" "$I/256.png" "$I/64.png" "$I/48.png" "$I/32.png" "$I/16.png")
gob windows amd64 "$d/MvC2 EBOOT Builder.exe"
rm -f "$SYSO"
how "$d" <<'TXT'
Unzip first. Drag the disc image onto "MvC2 EBOOT Builder.exe". The builder is not
signed, so the first time Windows may show "Windows protected your PC"
(SmartScreen): click More info, then Run anyway.

Command line: "MvC2 EBOOT Builder.exe" "C:\path\to\game.iso"
TXT
(cd "$W/win" && rm -f "$OUT/$NAME-windows.zip" && zip -qr -X "$OUT/$NAME-windows.zip" "$NAME")

for a in x64:amd64 arm64:arm64; do
  d=$(dist "linux-${a%%:*}"); mkdir "$d/base"; cp "$BASE" "$d/base/EBOOT.PBP"
  gob linux "${a#*:}" "$d/mvc2-eboot-builder"
  cp "$I/512.png" "$d/mvc2-eboot-builder.png"
  cp "$REL/install-launcher.sh" "$d/"
  how "$d" <<'TXT'
Double-click mvc2-eboot-builder (or drop the disc image on it); it opens in a
terminal window, and the file picker needs zenity. If it will not start, make
it executable: chmod +x mvc2-eboot-builder

To add it to your applications menu, with its icon: ./install-launcher.sh

Command line: ./mvc2-eboot-builder ~/path/to/game.iso
TXT
  tar -C "$W/linux-${a%%:*}" -czf "$OUT/$NAME-linux-${a%%:*}.tar.gz" "$NAME"
done

d=$(dist mac); app="$d/MvC2 EBOOT Builder.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
gob darwin arm64 "$W/b-arm64"; gob darwin amd64 "$W/b-amd64"
lipo -create -output "$app/Contents/MacOS/mvc2-eboot-builder" "$W/b-arm64" "$W/b-amd64"
cp "$BASE" "$app/Contents/Resources/EBOOT.PBP"
cp "$I/AppIcon.icns" "$app/Contents/Resources/"
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>MvC2 EBOOT Builder</string>
<key>CFBundleIdentifier</key><string>io.github.mvc2-psp.builder</string>
<key>CFBundleIconFile</key><string>AppIcon</string>
<key>CFBundleExecutable</key><string>mvc2-eboot-builder</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$VER</string>
<key>LSMinimumSystemVersion</key><string>11.0</string>
</dict></plist>
PLIST
codesign --force --deep -s - "$app"   # ad-hoc: arm64 refuses unsigned code
how "$d" <<'TXT'
Unzip first, then open "MvC2 EBOOT Builder.app" and choose the disc image. The app
is not signed by an Apple developer, so the first time macOS may say it
cannot be opened: right-click the app, choose Open, then Open again. On
macOS 15 and later, open it once, then click Open Anyway in System Settings >
Privacy & Security.

Command line:
  "MvC2 EBOOT Builder.app/Contents/MacOS/mvc2-eboot-builder" ~/path/to/game.iso
TXT
(cd "$W/mac" && rm -f "$OUT/$NAME-macos.zip" && ditto -c -k --keepParent "$NAME" "$OUT/$NAME-macos.zip")

d=$(dist py); mkdir -p "$d/base" "$d/port/psp/pack" "$d/port/psp/tools" "$d/port/tools/convert"
cp "$BASE" "$d/base/EBOOT.PBP"
cp "$ROOT/port/psp/pack/psp_pack.py" "$d/port/psp/pack/"
cp "$ROOT/port/psp/tools/xmb_art.py" "$d/port/psp/tools/"
cp "$ROOT/port/tools/mvc2_lz16.py" "$d/port/tools/"
for f in iso afs png tim2 textures; do cp "$ROOT/port/tools/convert/$f.py" "$d/port/tools/convert/"; done
how "$d" <<'TXT'
This package has no builder: use Python 3 (any OS), in this folder:

  python3 port/psp/tools/xmb_art.py game.iso art
  mkdir MVC2
  python3 port/psp/pack/psp_pack.py --icon0 art/ICON0.PNG --pic1 art/PIC1.PNG game.iso base/EBOOT.PBP MVC2/EBOOT.PBP

Then continue from step 3.
TXT
(cd "$W/py" && rm -f "$OUT/$NAME-pypack.zip" && zip -qr -X "$OUT/$NAME-pypack.zip" "$NAME")

(cd "$OUT" && shasum -a 256 "$NAME"-* > SHA256SUMS && cat SHA256SUMS)
