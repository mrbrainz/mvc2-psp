# PSP build

## Toolchain (macOS, Apple Silicon)

The toolchain lives outside the repo and is never committed.

1. Download `pspdev-macos-latest-arm64.tar.gz` from
   <https://github.com/pspdev/pspdev/releases> (tested: v20261101, GCC 15.2.0)
   and unpack it to `~/Sites/pspdev`.
2. Install the Homebrew libraries it links against:
   `brew install libmpc gmp mpfr libusb readline gpgme libarchive openssl`
3. Environment (add to your shell profile):
   `export PSPDEV=~/Sites/pspdev PATH=$PSPDEV/bin:$PATH`
4. Emulator: `brew install --cask ppsspp-emulator` (PPSSPP 1.20.4, installs
   `/Applications/PPSSPPSDL.app`).

## Smoke test

`port/psp/hello` builds an EBOOT that prints "MvC2 PSP: toolchain OK".

    psp-cmake -S port/psp/hello -B build/psp-hello && cmake --build build/psp-hello
    /Applications/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL build/psp-hello/EBOOT.PBP

## Build

The PSP build is its own CMake project, `port/psp/CMakeLists.txt`, so no
upstream file changes. It includes `port/CMakeLists.txt` unchanged for the
game targets (`mvc2_game`, `mvc2_game_data`) and builds the glue, host core
and EBOOT itself. The game TUs need clang (clang-only flags; the EE float
rewrite works on LLVM IR), so they build with Homebrew LLVM's mipsel backend
(`brew install llvm`). `EMSDK` must point at the Emscripten layout below (the
signature pass takes its wasm path when emcc is installed):

    export EMSDK=~/Sites/emsdk-brew
    cmake -S port/psp -B build/port-psp -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/port/cmake/psp-clang.cmake
    ninja -C build/port-psp mvc2_eboot

All 18,440 game objects compile (MIPS II, single-float FPU, `-G0`,
`-mno-gpopt`: the source's `section(".sdata")` externs are aliases into the
EE image, out of gp range). PSP-side pieces, all in `port/psp/`:

- `tools/psp_glue.py`: runs upstream `gen_link_glue.py` with pspdev's newlib,
  libm and libgcc added to `link_catalog.HOST_LIBS`.
- `host/psp_entry.c` (psp-gcc, EABI): module entry, exit/power callbacks.
- `host/psp_main.c`, `libc_o32.c`, `psp_fpu.c`, `psp_prof.c` (clang o32).
- `port/platform/platform_psp.c`: loop and lifecycle.
- Link: psp-gcc, `-fno-use-linker-plugin -G0 --no-warn-mismatch`; the EE data
  units go in as a whole archive (their paths overflow the command line);
  `--wrap` for `fseeko`, `ps2_float_set_nearest` and the profiler stages.

## o32 / EABI32 boundary

clang has no EABI target, so all of our code (game, host, PSP platform layer)
is clang o32, and only calls into psp-gcc/PSP SDK code cross conventions.
psp-ld refuses the mix unless linked with `-Wl,--no-warn-mismatch`.

Rules (verified by `port/psp/abi_test` in PPSSPP 1.20.4, not yet on hardware):

- Direct call is safe with at most 4 int/pointer args and an
  int/pointer/void/float return; no varargs, 64-bit or struct-by-value args.
- Anything else goes through a psp-gcc shim taking one pointer to an argument
  struct (see `abi_shim.h`). An unshimmed 6-arg call returns garbage.
- SDK-to-our-code callbacks (threads, exit, audio) follow the same direct-call
  rule; the EABI caller's frame survives an o32 callee.
- newlib's libm is EABI too: a second float argument goes in `$f13` there but
  `$f14` in o32, so two-float libm calls (`fminf`, `fmaxf`) return garbage;
  `libc_o32.c` defines o32 versions. Add any other such function there.
- `libc_o32.c` defines o32 `snprintf`/`vsnprintf`/`fprintf`/`vfprintf`/`sscanf`
  for the whole link, so EABI code (psp-gcc files, PSPSDK libraries) must not
  call those: use newlib's `sniprintf` or `pspDebugScreenPrintf` there.

    psp-cmake -S port/psp/abi_test -B build/psp-abi && cmake --build build/psp-abi

## Runner

`psp_main.c` runs real time by default (boot, then the platform loop until
HOME -> Exit). `env.txt` next to the EBOOT holds testing switches, one
`KEY=VALUE` per line: `FRAMES=N` (headless, logic hash every 60 frames),
`QUIT_AFTER=N`, `PROFILE=1`, and any `MVC2_*` variable (setenv).

Run in PPSSPP (memory stick is `~/.config/ppsspp`):

    G=~/.config/ppsspp/PSP/GAME/mvc2; mkdir -p $G
    cp build/port-psp/EBOOT.PBP $G/
    ln -sf "$PWD/<your ISO>" $G/mvc2.iso     # never copied into git
    /Applications/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL $G/EBOOT.PBP
    # output: $G/mvc2_psp.log (state.bin only with MVC2_STATE_DUMP in env.txt)

The game data comes from the player's own PS2 disc, read through sceIo
(`psp_disc.c`, `psp_disc_io.c`): `DISC=path` in `env.txt`, else the data
packed into the EBOOT, else `ms0:/PSP/GAME/mvc2/mvc2.iso`, else
`ms0:/ISO/mvc2.iso`. To pack it (Python 3; only `/PS2/AFS00-03.AFS`, 193 MB,
the PSP build reads nothing else, as a minimal ISO in DATA.PSAR):

    port/psp/pack/pack_eboot.sh [your .iso] [EBOOT.PBP in] [out]
    # defaults: "Marvel vs. Capcom 2 - New Age of Heroes (USA).iso" in the
    # project root, build/port-psp/EBOOT.PBP -> build/port-psp/packed/EBOOT.PBP
    # (any OS: python3 port/psp/pack/psp_pack.py <iso> <in> <out>)

A packed EBOOT holds copyrighted data: never commit or share it. `[disc]` lines log
KB read and throughput every 5 s.

`ra_state_hash` covers host pointers, so it only compares runs of one binary.

## PSP vs wasm state check

`compare.py` is for port vs oracle; between two ports its block hashes flag
pointers neither side can translate (function pointers, host malloc blocks),
and wasm's `fix()` mistakes integers such as `0x01000000` for heap addresses.
`compare_ports.py` compares a word only when both its translated and raw
values differ, using upstream's outputs: the per-frame dump (player records
and game block, every frame) and `MVC2_STATE_SNAP` snapshots (BSS and heap,
translated + `.raw`, up to 8 frames per run, ~52 MB each):

    FR="99 199 299 399 499 599"
    PS=$(for f in $FR; do printf "$f:ms0:/PSP/GAME/mvc2/snap/s$f.bin,"; done)
    RS=$(for f in $FR; do printf "$f:ref/s$f.bin,"; done)
    mkdir -p $G/snap ref
    printf "FRAMES=600\nMVC2_STATE_DUMP=ms0:/PSP/GAME/mvc2/snap/dump.bin\nMVC2_STATE_SNAP=${PS%,}\n" > $G/env.txt
    MVC2_STATE_DUMP=ref/dump.bin MVC2_STATE_SNAP=${RS%,} node build/port-em/mvc2_host.js --iso <ISO> --frames 600
    python3 port/tools/oracle/compare_ports.py $G/snap/dump.bin ref/dump.bin \
        $(for f in $FR; do printf -- "--snap $G/snap/s$f.bin ref/s$f.bin "; done) \
        --ignore port/tools/oracle/psp_vs_wasm.ignore

Result (attract mode, no input): 600 frames of player records and game block
plus six 26 MB snapshots, 0 diverging words. 77 words differ as host
pointers, 31 as host pointers with the EE uncached segment bits the game ORs
on for DMA (`0x20000000`/`0x30000000`), and 3 ignore-listed words that vary
between any two runs (boot clock, timer, one malloc chunk size).

## Reference build (wasm, under node)

`brew install emscripten`; the port's tools expect an emsdk layout, so point
`EMSDK` at a folder of symlinks:

    E=~/Sites/emsdk-brew; L=/opt/homebrew/opt/emscripten/libexec
    mkdir -p $E/upstream; ln -sfn $L/llvm/bin $E/upstream/bin; ln -sfn $L $E/upstream/emscripten
    EMSDK=$E emcmake cmake -S port -B build/port-em -G Ninja -DMVC2_APP=OFF
    EMSDK=$E ninja -C build/port-em mvc2_host

## Memory budget (P04)

Measured in PPSSPP (model PSP-2000+, `MEMSIZE=1`), attract mode, 600 frames:

| | MB |
|---|---:|
| static image (`.text` 5.8, `.data` 0.6, `.bss` 3.05) | 9.5 |
| malloc after boot | 28.2 |
| malloc after 600 frames | 28.3 |
| total with stacks | ~38.5 |

Almost all of the malloc is the EE heap (`HOST_EE_HEAP`, 28 MB,
`port/host/boot.c`), which the game fills at boot with its 22 MB arena and
6 MB handle pool, so play does not grow it. **Minimum model: PSP-2000 (Slim)
or later, with `MEMSIZE=1`** (about 52 MB user memory, about 13 MB left for
renderer and audio buffers; VRAM is separate). The PSP-1000 (24 MB user)
cannot hold the EE heap. `[mem]` lines in `mvc2_psp.log` report malloc use;
"malloc arena" is newlib's arena so far (it grows on demand), not the heap
limit, and "kernel free" is what `PSP_HEAP_SIZE_KB(-1024)` leaves outside the
heap. With `DRAWLOG=1`, every 600 frames: renderer bytes by user and the
largest block malloc can still give. 3v3 fight in PPSSPP (P27): textures
2.5 MB, character atlases 0.6 MB, mesh buffers 0.8 MB, CLUTs 0.1 MB, **14 MB
of heap still free**. Six different characters at the 1 MB atlas cap would
add about 5 MB.

## Platform shell (P11)

`port/platform/platform_psp.c` implements the loop and lifecycle parts of
`platform.h` (input: P12, renderer: P21). Logic steps are locked to the
59.94 Hz display (the NAOMI's rate): one step per vblank, one more per missed
vblank after an overrun (vblank counter, not wall clock), capped at
`plat_set_max_steps`; render frames drop, logic frames do not. HOME -> Exit
sets a flag the loop polls (3 s fallback), power suspend calls `on_pause`, and
the CPU runs at 333 MHz. `env.txt`: `FRAMES=N` runs headless; without it the
runner is real time; `QUIT_AFTER=N` fires the exit callback at frame N
(PPSSPP has no HOME menu).

Speed (PPSSPP 1.20.4, JIT, cycle-estimated time, attract mode, `-O2`):

| | ms per logic frame |
|---|---:|
| `-O0` (no build type) | 300 |
| `-O2`, 222 MHz | 140-180 |
| `-O2`, 222 MHz, `MVC2_PS2_FLOAT=OFF` | 129 |
| `-O2`, 333 MHz | 94-121 |

Budget is 16.7 ms. Not yet profiled or measured on hardware.

Builds now need `EMSDK=~/Sites/emsdk-brew` in the environment: with emcc on
PATH, the signature pass takes its wasm path.

## Logic frame profile (P09)

`PROFILE=1` in `env.txt` logs the average ms per `mvc2_frame` stage every 300
frames (`psp_prof.c`: the stage calls are wrapped at link time). Attract mode, 333 MHz,
PPSSPP:

| | title/logos | attract scenes | attract fight demo |
|---|---:|---:|---:|
| with `ps2_float.c` helpers | 6.8 ms | up to 33 ms (render pass 1) | 26-38 ms (game update) |
| native FPU (`MVC2_PS2_FLOAT=OFF`) | 0.5 ms | 0.3 ms | 0.3-0.6 ms |

(The earlier 94-121 ms included an always-on state dump of the whole BSS
every frame, now only with `MVC2_STATE_DUMP` in `env.txt`.)

The helpers compute in double, which on the PSP is libgcc soft-float, so they
are almost the whole cost. Their semantics (round toward zero, denormals to
zero, overflow to the largest finite) are what the Allegrex FPU gives with
FCSR RM = 1 and FS = 1. The comparison (with a test switch, since removed):

| PSP variant vs wasm (helpers on), 600 frames | diverging words |
|---|---:|
| helpers | 0 |
| native FPU, RZ + flush (`FCSR 01000e01`) | 0 |
| native FPU, round to nearest (control) | 47, from frame 0 |

PSP builds do this by default (P10): `psp-clang.cmake` sets
`MVC2_PS2_FLOAT=OFF`, and `psp_fpu.c` programs FCSR at start-up: RM = 1 (EE)
or RM = 0 (`MVC2_PS2FP_OFF=1`), FS = 1 always. The arcade round-to-nearest
setting (`ps2_float_set_nearest`) is wrapped at link time to switch it too.
FCSR is per thread: any thread that runs game code must call
`psp_fpu_init()`. Result (separated build): 59.94 fps through the
1800-frame attract run (logic 0.24-0.56 ms/frame), and state as below.
Verified in PPSSPP only; hardware may differ on denormal inputs and division
by zero (EE: largest finite), so P42 checks on a device.

Fights and character select (P26): `fe_sync_handle` (upstream fe_render.c)
hashed about 1 KB of each texture and palette at a stride on every mesh draw
and texture upload, about 18 ms a frame on the PSP. `host/psp_fe_sync.c`
wraps it (and `func_003EA660`, which calls it inside its own file) to sync each
handle once per logic frame. Update, PPSSPP, no profiler: fight 9.7 ms,
character select 10.0-10.4 ms, 59.94 fps. Of the rest, the game's own VU1
packet build (`func_003F0500__game`) is about 5 ms and the GU mesh path 2 ms.
`PROFILE=1` also lists those nested costs.

## Renderer: 2D core (P21)

`port/render/r_gu.c` implements the `fl_*` API of `render.h` with sceGu, for
the PSP build only; upstream's `fe_render.c` binds the game's render-library
calls to it unchanged, and `r_gl.c` is not used. Its GL-free logic (state
setter, blend decode, primitive set-up, texture/palette tables) is a copy of
`r_gl.c`'s and has to be kept in step by hand. sceGu is EABI; the three calls
with more than four arguments go through `port/psp/host/psp_gu_shim.c`.

- Primitives are scaled straight into the 480x272 back buffer, fitting the
  game's displayed area (640x448) with its aspect kept (389x272, centred).
- Indexed textures use the GE's CLUT (T8 + 256-colour 8888 palette);
  textures live in RAM, padded to powers of two; > 512 is not drawn.
- Depth as the GS: larger is nearer, cleared to 0, GEQUAL.
- `sceGuScissor` takes x, y, width, height (not x1, y1).
- Not reproduced yet: perspective STQ on 2D triangles (affine), destination
  alpha blend factors (the PSP keeps stencil in alpha).

`DRAWLOG=1` in `env.txt` logs the frontend's draw counters per 60 frames.
Attract run: states 1 8 (save-data warning, ~38 sprites a frame) and 1 13
(ADX logo) draw correctly with nothing rejected; states 1 1 and 1 2 (CAPCOM
logo, title, demo fight) draw through the mesh and character paths, P24/P25.
59.94 fps, update 0.9-4.2 ms a frame (PPSSPP).

## Renderer: characters (P24)

`port/render/r_gu_char.c` is a PSP copy of `r_char.c`'s `fl_char_*` (kept in
step by hand); upstream `fe_char.c` is compiled unchanged on top. Tiles decode
on first use into 512x512 T4 pages in RAM (128 KB, at most 8 per bank; a full
bank clears its atlas); each bank's 16 palettes are 16-colour 8888 CLUTs,
drawn through `r_gu.c`'s `RM_RAW4` batch. `DRAWLOG` adds `char draws/parts dec
tile-decodes`.

## Controls (P12)

`plat_poll_input` (platform_psp.c) maps the PSP pad 1:1 onto player 1's PS2
pad; the game's own BUTTON CONFIG then assigns attacks (defaults below).

| PSP | PS2 | default |
|---|---|---|
| D-pad / analog stick | D-pad / left stick | move |
| Square / Triangle | Square / Triangle | LP / HP |
| Cross / Circle | Cross / Circle | LK / HK |
| L / R | L1 / R1 | A1 / A2 |
| Start / Select | Start / Select | |

PS2 L2/R2/L3/R3 have no PSP button. Player 2 is not connected.
`PRESS=frame:len:bits,...` in `env.txt` (testing) holds PS2 bits (hex,
`pad.h`) on player 1, e.g. `PRESS=600:6:8` presses Start at frame 600.

## Renderer: meshes (P25)

`port/render/r_gu_mesh.c` is a PSP copy of `r_mesh.c`'s clay path
(`fl_clay_*`; POL chunk parsing, VU1 descriptor selection, colour maths);
upstream `fe_mesh.c` is compiled unchanged on top. The GE transforms: one
matrix = world * view * projection * the func_003DD5D0 viewport * canvas to
screen (`r_gu_canvas_to_ge`), so UVs are perspective-correct. Each clay keeps
its POL vertices and a GE vertex buffer whose colours are recomputed only
when the 0x66 modulate, the format or a vertex write changes them; lit meshes
are coloured per draw. The VU1 trivial reject (ADC) is done on the CPU.
GE clip planes are off: they cull clip-space z < 0, where all of the game's
geometry lies. Not reproduced: fog (0x8000), UV-matrix cross terms, meshes
over 65536 vertices.

`STAGE=N` in `env.txt` (testing, 0-16) makes every stage load stage N.
All 17 stages (P25, scripted PRESS + STAGE runs): render in PPSSPP at 59.94
fps, fight update 5.9-10.9 ms.

`DRAWLOG` adds `mesh draws/triangles microseconds`; `MESH_DUMP=N` in
`env.txt` logs every mesh draw of frame N.
