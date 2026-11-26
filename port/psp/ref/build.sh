#!/bin/sh
# P42: build mvc2_psp_ref.js (ref_main.c) from an emscripten build of the port.
#   port/psp/ref/build.sh [BUILD_DIR]   (default build/port-em, after
#   `ninja -C build/port-em mvc2_host`; see port/docs/PSP.md "Reference build")
# Reuses mvc2_host's compiler and link line, with ref_main.c, psp_script.c and
# the frontend in place of port/stubs/host_main.c.  Output: BUILD_DIR/mvc2_psp_ref.js/.wasm.
set -e
R=$(cd "$(dirname "$0")/../../.." && pwd)
B=$(cd "${1:-$R/build/port-em}" && pwd)
CC=$(ninja -C "$B" -t commands mvc2_host | grep 'stubs/host_main.c.o -c' | awk '{print $1}')
LINK=$(ninja -C "$B" -t commands mvc2_host | tail -n 1)
mkdir -p "$B/psp_ref"
# the frontend the PSP links (fe_render, fe_mesh, fe_char) on the renderer over
# swgl and the mixer with no device, as port/generic/build.sh
OBJS=
for f in psp/ref/ref_main.c psp/host/psp_script.c psp/host/psp_1p.c psp/host/psp_credits.c host/fe/fe_render.c host/fe/fe_mesh.c host/fe/fe_char.c \
         host/fe/fe_audio.c render/r_gl.c render/r_char.c render/r_mesh.c render/decode.c render/r_font.c \
         render/swgl.c audio/adx.c audio/vag.c audio/hdbank.c audio/resample.c audio/synth.c audio/tsd.c \
         audio/audio.c audio/wav.c audio/dcse.c; do
  o="$B/psp_ref/$(basename "$f" .c).o"
  "$CC" -O2 -fno-common -DPORT_SWGL -DPORT_NO_AUDIO_DEVICE -I"$R/port/stubs" -I"$R/port/host" -I"$R/port/platform" \
        -I"$R/port/psp/host" -I"$B/glue" -c "$R/port/$f" -o "$o"
  OBJS="$OBJS $o"
done
L=$(printf '%s\n' "$LINK" | sed -e "s| CMakeFiles/mvc2_host.dir/stubs/host_main.c.o |$OBJS |" \
      -e 's|-o mvc2_host.js|-o mvc2_psp_ref.js -Wl,--wrap=func_00130040,--wrap=func_00128CA0,--wrap=func_00149690,--wrap=func_00149020,--wrap=func_0016B440|' -e 's|mvc2_host.map|mvc2_psp_ref.map|')
case "$L" in *ref_main.o*mvc2_psp_ref.js*) ;; *) echo "build.sh: mvc2_host link line not as expected" >&2; exit 1 ;; esac
(cd "$B" && eval "$L")
echo "$B/mvc2_psp_ref.js"
