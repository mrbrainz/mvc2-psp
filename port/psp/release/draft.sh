#!/bin/sh
# Local release draft, to test the EBOOT builder before tagging a release.
#   port/psp/release/draft.sh [VERSION]      (default dev-<commit>)
# Builds the release base EBOOT (build/port-psp-rel, as CI does), then
# replaces private/release-drafts/ with:
#   packages/   the five packages and SHA256SUMS (package.sh)
#   unpacked/   this computer's package, extracted, ready to run on an .iso
#   snd0/       the XMB music encoder for this computer (other packages: none)
# Needs what package.sh needs, plus EMSDK (port/docs/PSP.md).
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
VER=${1:-dev-$(git -C "$ROOT" rev-parse --short HEAD)}
D=$ROOT/private/release-drafts
B=$ROOT/build/port-psp-rel
[ -f "$B/build.ninja" ] || cmake -S "$ROOT/port/psp" -B "$B" -G Ninja -DMVC2_RELEASE=ON \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/port/cmake/psp-clang.cmake"
ninja -C "$B" mvc2_eboot
# the XMB music encoder: this computer's only (CI builds all four)
case "$(uname -s)-$(uname -m)" in
Darwin-*)      H=macos ;;
Linux-x86_64)  H=linux-x64 ;;
Linux-aarch64) H=linux-arm64 ;;
*)             H=other ;;
esac
cmake -S "$ROOT/port/psp/snd0" -B "$ROOT/build/snd0" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build/snd0" --config Release
rm -rf "$D"; mkdir -p "$D/unpacked" "$D/snd0"
cp -R "$ROOT/build/snd0/bin" "$D/snd0/$H"
export SND0_DIR="$D/snd0"
"$ROOT/port/psp/release/package.sh" "$VER" "$B/EBOOT.PBP" "$D/packages"
case "$(uname -s)-$(uname -m)" in
Darwin-*)        ditto -x -k "$D/packages/mvc2-psp-$VER-macos.zip" "$D/unpacked" ;;
Linux-x86_64)    tar -C "$D/unpacked" -xzf "$D/packages/mvc2-psp-$VER-linux-x64.tar.gz" ;;
Linux-aarch64)   tar -C "$D/unpacked" -xzf "$D/packages/mvc2-psp-$VER-linux-arm64.tar.gz" ;;
*)               echo "no package to unpack for $(uname -sm)" ;;
esac
echo "draft $VER: $D"
