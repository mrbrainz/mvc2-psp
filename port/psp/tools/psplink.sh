#!/bin/sh
# Run the PSP build on hardware over USB with PSPLink (testing).
#   port/psp/tools/psplink.sh host    start usbhostfs_pc on private/psplink/host (keep running)
#   port/psp/tools/psplink.sh run     (re)start build/port-psp/mvc2_psp.prx on the PSP
#   port/psp/tools/psplink.sh sh CMD  any pspsh command (reset, modlist, ...)
# host0: holds mvc2_psp.prx (the build), mvc2.iso, env.txt; the log,
# shots and settings are written there too (psp_game_dir, psp_entry.c).
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
PSPDEV=${PSPDEV:-$(cd "$ROOT/../pspdev" 2>/dev/null && pwd)}
H="$ROOT/private/psplink/host"
mkdir -p "$H"
ln -sf "$ROOT/build/port-psp/mvc2_psp.prx" "$H/mvc2_psp.prx"   # PSPLink cannot start the PBP (0x80020148)
[ -e "$H/mvc2.iso" ] || ln -sf "$(ls "$ROOT"/*.iso | head -1)" "$H/mvc2.iso"
[ -e "$H/env.txt" ] || printf 'PROFILE=1\n' > "$H/env.txt"
case "$1" in
host) exec "$PSPDEV/bin/usbhostfs_pc" "$H" ;;
run)  "$PSPDEV/bin/pspsh" -n -e reset >/dev/null 2>&1 || true; sleep 3
      exec "$PSPDEV/bin/pspsh" -n -e "./mvc2_psp.prx" ;;
sh)   shift; exec "$PSPDEV/bin/pspsh" -n -e "$*" ;;
*)    sed -n 2,6p "$0"; exit 1 ;;
esac
