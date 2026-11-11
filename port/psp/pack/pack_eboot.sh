#!/bin/sh
# Pack the game data from your own MvC2 PS2 disc image into the PSP EBOOT.PBP
# (psp_pack.py; needs Python 3). Defaults: the ISO in the project root,
# build/port-psp/EBOOT.PBP in, build/port-psp/packed/EBOOT.PBP out.
#   usage: pack_eboot.sh [your .iso] [EBOOT.PBP in] [EBOOT.PBP out]
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
iso=${1:-"$root/Marvel vs. Capcom 2 - New Age of Heroes (USA).iso"}
in=${2:-"$root/build/port-psp/EBOOT.PBP"}
out=${3:-"$root/build/port-psp/packed/EBOOT.PBP"}
mkdir -p "$(dirname "$out")"
py=python3; command -v $py >/dev/null 2>&1 || py=python
exec $py "$here/psp_pack.py" "$iso" "$in" "$out"
