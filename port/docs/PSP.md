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

Release base EBOOT (no XMB art, no game data; never reads the ISO): configure
with `-DMVC2_RELEASE=ON`, then `python3 port/psp/tools/check_release_eboot.py EBOOT.PBP`.

Player-facing builder (Go, no runtime; `port/psp/builder/`): packs the release
base EBOOT with the disc's XMB art and game data, checking the AFS SHA-1s.

    cd port/psp/builder && go build -o mvc2-eboot-builder .
    ./mvc2-eboot-builder [-base EBOOT.PBP] [-o OUTDIR] [-q] [game.iso]   # -> OUTDIR/MVC2/EBOOT.PBP

Release packages: `port/psp/release/package.sh VERSION BASE_EBOOT OUTDIR`
(macOS: needs go, lipo, codesign, ditto). CI (`.github/workflows/psp-release.yml`)
runs it on a `v*` tag and attaches the packages to a draft GitHub release;
a manual run only uploads them as workflow artifacts.

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

## Determinism check (P42, 2026-11-26)

Reference: `port/psp/ref` links the wasm port's objects (`build/port-em`,
`ninja mvc2_host`) with the PSP's game-flow changes: its script runner and
frame step, `psp_1p.c`, `psp_credits.c`, the boot card screen as with no
savedata, and the renderer on swgl. Fuzz scenarios (`fuzz.py show SEED`) run
on both; the dumps are aligned on the resync record (state 1/1/0) and
compared with `compare_ports.py`.

    port/psp/ref/build.sh            # -> build/port-em/mvc2_psp_ref.js
    MVC2_P1_TEAM=.. MVC2_CPU_TEAM=.. MVC2_PAD_DELAY=0 MVC2_RESYNC=1/1/0 MVC2_STATE_DUMP=ref.bin \
        node build/port-em/mvc2_psp_ref.js --iso <ISO> --frames N --script script.txt
    # PSP: the same MVC2_* lines in env.txt, plus SCRIPT=, QUIT_AFTER=N,
    # NO_FRAME_SKIP=1, MVC2_NO_SAVEDATA=1 (boot as with no savedata)

Results (PPSSPP, `unlock=all`, seeds 1, 17, 40, 240; seed 1 also PSP-2003):
boot, menus, character select and the first fight frames match, pads, rand
seed and game RNG included. Then:
- **1, 240:** 1-2 ulp float drift in players' +0x38/+0x60/+0xF4 from a
  tag-in (frame 2898 in seed 1), no split: 17 / 14 words.
- **17, 40:** the same kind of drift (seed 17 frame 2682, player 1 +0x5C),
  then a split (game RNG differs from frames 3889 / 2400).
- **Cause found (seed 1):** clang -O2 folds float expressions it can prove
  constant to nearest; the EE computes them at run time toward zero
  (func_00186900, `(20.0f * f6C) / 2.0f`; modelled exactly). `-frounding-math`
  and `-ffp-model=strict` do not stop it for this target (P122).
- **Fixed on the way:** libgcc's `__truncdfsf2` rounds to nearest whatever
  FCSR says; it now follows FCSR (`psp_fpu.c`), as `dptofp` does for the
  game; this removed the drift of the matrix at D_004F6CD0+0x40. newlib's
  `cosf` differs from musl's (the wasm libc) by an ulp: `psp_libm.c` has
  musl's `sinf`/`cosf` (round to nearest for the call). Neither alone fixes
  the drift above.
- **Checked:** the Allegrex FPU (PPSSPP and PSP-2003) rounds `add.s`,
  `mul.s`, `div.s`, `sqrt.s`, `cvt.s.w` toward zero under FCSR RM 1; libgcc's
  double arithmetic rounds to nearest, as wasm's.
- **Limits:** on hardware the real pad's analog nub reaches the game
  (player 0 +0x510 differed); the P101 hot build stops at frame ~2100 of these
  scripts on both PPSSPP and hardware.

Build-time float folding (P122, 2026-11-26):
- **Change:** `port/psp/cmake/psp-fp-cc.pl`, the compiler launcher of
  `mvc2_game_code` and `psp_host_core` (`MVC2_PSP_STRICT_FP`, on;
  `MVC2_NO_PSPFP=1` in the build environment skips it). It emits the front
  end's IR (`-Xclang -disable-llvm-passes`, `-ffp-contract=off`), turns float
  fadd/fsub/fmul/fdiv, int->float, double->float and sqrt into constrained
  intrinsics with dynamic rounding, makes the module strictfp, then compiles
  the IR at -O2. `func_00186900` now multiplies at run time.
- **Not `-frounding-math` alone** (forced with `-Xclang
  -fexperimental-strict-floating-point`): it also leaves the front end's
  conversions of literals to run time, so `(float)4.1666667908e-02` in
  newlib's `cosf` went through `__truncdfsf2` toward zero (one ulp low, six
  libcalls per call; seed 40's first difference, frame 2357).
- **Seeds (PPSSPP, `unlock=all`):** 1 matches (0 words); 240 matches but for
  one word (frame 4755, 0x004F2D18, 1 ulp); 17 and 40 still split at frames
  3889 / 2400. Seed 40 has EE BSS identical through 2399 (GS packet words
  aside), then a character takes `jtbl_00458680[4]` (`func_002A5D10`, a
  15-particle effect) on the PSP only, also with every PSP-side replacement
  set to the game's body: an input outside EE BSS (P123).
- **PSP-2003 (`fight6`, `unlock=all`, `PROFILE=1 NO_FRAME_SKIP=1`, two runs
  each against a build with the launcher off):** fight windows (frames
  1800-3000) 12.8-19.4 ms on both, every window within 0.15 ms; character
  select windows (900, 1200) +0.2-0.5 ms. Shots not compared.

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

Almost all of the malloc is the EE heap (`HOST_EE_HEAP`, 28 MB in
`port/host/boot.c`; 23 MB on the PSP, `psp_ee_heap.c` over its sbrk with
`--wrap`, P55), where the game puts its 22 MB arena (the 6 MB handle pool is
inside it) at boot, so play does not grow it. A refused growth logs
`[mem] EE heap: sbrk(...) refused`. **Minimum model: PSP-2000 (Slim)
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

Sound effects stay as SPU2 ADPCM in the loaded bank (`port/audio/vag_psp.c`,
`--wrap` of `vag_decode`/`vag_free`/`dcse_apply`): `mix_psp.c` decodes each
voice's blocks as it plays, sample-identical to `vag.c`, instead of holding
~5 MB of 16-bit PCM. Only the samples the Dreamcast set replaces become PCM.
Full arcade run (P53): largest free block never below 3.1 MB.
At the end of a run `[hwm]` lines (`psp_ee_hwm.c`, P54) give the EE heap
footprint: full arcade, the arena has 16.3 MB of 22 MB written (the 6 MB
handle pool inside it, 4.6 MB), and the game's break stops at 22.5 MB. That
rules out the PSP-1000. With the 23 MB EE heap (P55) the largest free block
never drops below 8 MB.
`-DMVC2_PSP_HEAPMAP=ON` logs free gaps and live bytes by caller with
`DRAWLOG=1`.

### PSP-1000 budget (P73)

2026-11-22, PPSSPP 1.20.4, `arcade_psp.txt` full run (28,380 frames),
PSP-2000 model with `DRAWLOG=1`, plus a `HEAPMAP` build for the callers. Peaks
over the run; the PSP-1000 (24 MB user partition, about 23.3 MB once the
1 MB kernel reserve is taken out) was booted in PPSSPP only, and failed at
the EE heap allocation. Not measured on hardware.

| Consumer | Now (MB) | Low target (MB) | How (ticket) |
|---|---:|---:|---|
| Program image: `.text` 3.5, `.data` 0.6, `.bss` 3.1 (was 8.9 before P77) | 7.3 | 5.5 | `.bss` holds a 1 MB GE display list and the 1.5 MB EE BSS (P77 done) |
| Thread stacks (main, audio 64 KB, watchdog, callback) | 0.3 | 0.3 | |
| EE heap (game arena 22 MB, 6 MB handle pool inside it) | 23.0 | 16.5 | arena written 16.2 MB, highest offset 21.3; pool written 4.4 (P75) |
| Music: whole ADX track in one `disc_load` block | 4.7 | 0.1 | stream it from the disc (P78) |
| Renderer: textures 4.0, char pages 1.6, meshes 0.85, CLUTs 0.15 | 5.7 | 2.5 | (P76) |
| Sound banks (`hd_bank_load`) | 1.4 | 1.4 | |
| Other malloc | 0.8 | 0.5 | |
| **Total** | **44.8** | **26.8** | PSP-1000 has about 23.3 |

Even at these targets the PSP-1000 is short by about 3.5 MB. The EE arena
target is the pages it has ever written, not its peak live use, which is not
measured yet (P75 has to measure it). It fits only if the arena's live peak
is about 13 MB or lower, or if game data is loaded on demand (P80).

### Memory profile (P74)

2026-11-26. `psp_memprofile.c` picks the profile before the boot from the
largest block `malloc` gets (the PSPSDK heap is all user memory but 1 MB,
minus the program image): under 32 MB is `low` (P73 targets), otherwise
`full` (today's sizes). `[mem] profile ...` in the log. Only the EE heap
size comes from it so far (23 MB full, 16.5 MB low); P75-P78 add theirs.
- **PSP-2000 model (PPSSPP):** 48,512 KB, `full`; `fight6` state dump
  identical to the build before (3,057 frames, 0 words).
- **PSP-1000 (hardware, PSPLink):** 15,744 KB, `low`. That is the room left
  for every heap allocation at boot (24 MB partition less the 7.3 MB image
  and the 1 MB kernel reserve); the 16.5 MB EE heap does not fit, so the
  boot still stops there (P75).
- The probe keeps its pointer `volatile`: clang otherwise removes an unused
  `malloc`/`free` pair and takes the allocation as successful.

### Program image (P77)

2026-11-23, `llvm-readelf -S` on the PRX: 8.97 → 7.31 MB (`.text` 3.49,
`.data` 0.62, `.bss` 4.76 → 3.10). Two changes:

- The oracle state buffer (1.48 MB, used only with `MVC2_STATE_DUMP`) is now
  allocated when first used. Upstream `dump_format.h` is unchanged. The build
  compiles `state_dump.c` from a patched copy instead: see
  `port/psp/patches/apply_upstream_patches.cmake`. That script stops the build
  if the text it patches has changed upstream.
- `r_gu.c` sizes its texture and palette tables to the game's records, 384 and
  256 entries, saving about 175 KB.

Kept as they were:

- The 1 MB display list: its measured peak is 958 KB.
- `--gc-sections`: it drops about 7,900 EE data symbols that the game reaches
  through EE addresses.

How it was checked, in PPSSPP with the full `arcade_psp.txt`:

- Frame times with `PROFILE=1` are unchanged: mean update 31.78 ms before,
  31.64 ms after.
- An `MVC2_STATE_DUMP` comparison (`compare_ports.py`) of this build against
  the previous one found 0 diverging words over 21,498 frames.
- Snapshots at frames 3400 and 14200 match up to EE 0x01D34380, where the PSP's
  23 MB EE heap ends. `state_dump.c` assumes the 28 MB upstream heap, so a PSP
  snapshot past that address holds host memory, not game state.

Not checked on hardware.

## PSPLink (hardware over USB)

Build the PSP side from source (`git clone https://github.com/pspdev/psplinkusb`
into `private/psplink/src`, `make -f Makefile.psp`) and copy `psplink.prx`,
`psplink.ini`, `psplink_user.prx`, `usbhostfs.prx`, `usbgdb.prx` and
`bootstrap/EBOOT.PBP` to `ms0:/PSP/GAME/PSPLINK/`; start it from the XMB, USB
cable in, not in USB-storage mode. Mac: `brew install libusb`, then

    port/psp/tools/psplink.sh host    # keep running
    port/psp/tools/psplink.sh run     # starts build/port-psp/mvc2_psp.prx from host0: (not the PBP: 0x80020148)

`host0:` is `private/psplink/host`: the build, the ISO (symlinks), `env.txt`,
and the log, shots and settings the run writes. `psp-gdb` works through
`usbgdb.prx`.

Textures (P87) are copied into the VRAM past the frame and depth buffers on
first bind (about 600 KB; reset at a frame boundary when full, off while a
system dialog is up): the GE reading them from RAM slowed the CPU through bus
contention (3v3 fight: frame 45.2 -> 42.5 ms). `MVC2_NO_VRAM_TEX=1` turns it off.

Texture/palette sync (P85, `psp_fe_sync.c`): descriptors, palettes and each
texture's last byte are checked every frame, a quarter of the texture samples
in turn; the real sync runs on any change. A texture rewritten without the
game's unlock call can show up to 4 frames late. `MVC2_EXACT_TEX_SYNC=1`
restores the per-frame upstream check. The display list is written through
the uncached mirror: the GE reads a GU_DIRECT list while it is being built.

Mesh draw body (P84, `psp_packet.c`): the game's func_003F0500 builds VU1
packets per draw (microcode setup, vertex/UV packets, DMA tags and kicks)
that the PSP renderer never reads; the PSP body keeps the format word and
packet rebuild, texture selection and the globals the body sets, and skips
the rest (3v3 fight: frame 45.3 -> 39.8 ms). `MVC2_PACKET_FULL=1` runs the
game's own body.

## Hardware profile (P86)

PSP-2003, 333 MHz, PROFILE=1 over PSPLink, after P83-P85/P87 (ms a frame):

| Scene | Frame | Mesh draw | fl_clay_draw | Packet build | Tex select | Other render |
|---|---|---|---|---|---|---|
| Intro, title | 4-17 (59.94 fps) | - | - | - | - | - |
| Attract demo fight | 25-27 | 13 | 7.6 | 3.7 | 1.8 | 5 |
| Character select | 38 | 20 | 13 | 2.7 | 0.7 | 12 |
| 3v3 fight | 36-50 | 28 | 15 | 9 | 4.5 | 9 |

Mesh draw = fl_clay_draw + packet build + the PSP mesh wrapper (fe_mesh.c
func_003F0500, ~4 ms in fights). Game update is 1-2 ms; the GE waits
~0.2 ms: the frame is CPU-bound in rendering. Next: P88-P94.

Mesh wrapper (P89): 2.6-3.1 ms real in fights (profiler syscalls ~1 us each
inflated P86's figure); change guards in r_gu.c save ~0.5 ms; the rest is
fe_mesh.c's per-draw hand-off (P96).

Mesh hand-off (P96, 2026-11-23):
- **What changed:** the PSP builds a patched copy of `fe_mesh.c`
  (`port/psp/patches`). Its 8 per-draw state calls become
  `psp_mesh_handoff()`, and the renderer reads the game's view, projection,
  world, UV and clip matrices in place. It copies lights only for lit draws.
- **Scripted run (PPSSPP):** the fight wrapper went from 1.24 to 0.68 ms a
  frame. That was measured with `PROFILE=1` and A/B against the upstream
  hand-off (`MVC2_MESH_HANDOFF=1`).
- **Check mode:** with `MVC2_MESH_CHECK=1`, 1.56 M draws all read exactly the
  bytes the hand-off would have copied.
- **Game state:** an `MVC2_STATE_DUMP` A/B over 6,000 headless frames found
  0 diverging words up to the end of the EE heap.
- **PSP-2003 (`fight6`, `PROFILE=1`, A/B as above, two runs each):** the
  wrapper's profile figure went 3.52 -> 2.26 ms, then 3.53 -> 2.15 ms on the
  repeat. Heaviest blocks: 5.3 -> 3.3-3.5 ms.
- **Profiler cost:** each timer read is a syscall of 0.99 us, now printed at
  start-up as `[prof] timer read`. About 5 reads fall inside each draw's
  wrapper window, and fights average 108 draws a frame (169 at most, counted
  with `PROFILE=2`). That is about 0.5 ms of the figure.
- **Real cost:** about 3.0 -> 1.6 ms a frame, or about 15 us a draw. That is
  above the ticket's 1 ms target.
- **Where the rest goes (temporary timers, hardware):** format rebuilds 0 in
  the whole fight; vertex pulls for about 3 clays a frame; about one texture a
  draw, which `fe_sync_handle`'s same-frame check returns early; and the
  `fe_sync_state` origin and mode-word copy. No single part is large, so the
  rest is spread over small per-draw calls, probably cache-miss bound.
- **Merged pre-draw (2026-11-23):** the patch now replaces everything
  between the game's body and `fl_clay_draw` with one call,
  `psp_mesh_predraw()`. That covers the origin and mode-word sync, the
  texture re-sync, the hand-off and the dump/verbose logs, and the upstream
  lines stay under `#if 0`. Timed separately, the remaining pieces were
  `fe_sync_state` (about 1 mode-word change a frame, never with a 2D batch to
  flush), the logging guards, and under 0.1 ms of texture re-sync: call and
  cache-miss cost, too small to time. PSP-2003 `fight6`, `NO_FRAME_SKIP=1`,
  no `PROFILE`, two runs each: character select 28.7-29.0 -> 27.2 ms and
  22.5-22.6 -> 21.9-22.1 ms a step; fights within noise. One light block
  (36 draws, 60 fps either way) went 7.0 -> 7.5 ms, likely a code-layout
  shift (the P101 ordering predates the new function). The profiled wrapper
  in fights is 1.4-1.8 ms, about 0.2 ms less than before. In PPSSPP:
  `MVC2_MESH_CHECK` 0 changed, state dump 3,057 frames 0 diverging, shots
  identical.
- **PSP-1000:** crashes during boot after the 23 MB EE heap (about 700 KB
  free), before any frame (P74-P79).
- **Kept:** the per-draw texture re-sync and `fe_sync_state` (see above).
  `ra_state_hash` differs between two launches of the same PSP build, because
  it includes host pointers, so it can't serve as a PSP trace. Use
  `MVC2_STATE_DUMP` instead.

Tex select (P90, fight6): 3.8-4.9 -> 1.0-1.4 ms in fights (O(1) resident
lookup, psp_reslist.c); the rest is GS upload bookkeeping (P95).

VRAM texture cache policy (P88, 2026-11-23, PSP-2003, `fight6`):
- **Before:** the P87 cache was reset at the end of any frame in which it was
  full. Fights use more textures than the ~690 KB past the frame and depth
  buffers, so every shown frame recopied ~690 KB (~7-8 ms) and still served
  about half the binds from RAM.
- **Now:** it is kept while that frame used at least half of what it holds.
  It resets when most of it went unused (a new scene) or for the system
  dialog. When a texture changes, its VRAM copy is updated in place.
- **Result:** fight display 9.2-12 -> 13-15 fps; character select 6.7-7.5 ->
  8.2-8.6. `fl_clay_draw` per drawn step (`NO_FRAME_SKIP=1`) 10.6-12.2 ->
  10.0-11.6 ms. The screenshot is identical.
- `MVC2_VRAM_RESET=1` restores the old policy.

Mesh screen matrix (P88, 2026-11-23, PSP-2003, `fight6`):
- **The change:** `fl_clay_draw` built the viewport matrix (several float
  divides), the canvas -> GE matrix and their product on every draw. Now it
  builds them only when the viewport or canvas changes. The arithmetic is the
  same, so the result is bit-identical.
- **Result:** `fl_clay_draw` per drawn step (`NO_FRAME_SKIP=1`, `PROFILE=1`)
  went 10.0-11.6 -> 8.9-10.2 ms. The normal-mode f6 screenshot is identical
  to the run before.
- **Tried and dropped:** caching world * (view * proj * screen) per camera was
  slower, because the camera matrices change between draws.

Mesh vertex writeback (P88, 2026-11-23, PSP-2003): `fl_clay_draw` wrote
each mesh's whole vertex buffer back from the D-cache on every draw. It now
does so only after the CPU has written the buffer, and never for
display-list (uncached) memory. Per drawn step: 8.9-10.2 -> 9.0-9.6 ms.

GE command writer (P102, 2026-11-23, PSP-2003, `fight6`, `PROFILE=2 NO_FRAME_SKIP=1`):
- **What changed:** `r_gu_mesh_draw` writes its texscale, projection,
  clip-plane and draw words straight into sceGu's list. It skips words the GE
  already holds and makes the stall-address syscall every 8th mesh draw, not
  on every draw. `MVC2_GE_SCEGU=1` restores the sceGu calls. `MVC2_GE_CHECK=1`
  compares the words with sceGu's: 344k draws in PPSSPP, 0 differ. The
  fight6 shots f1-f6 are byte-identical.
- **Bench, GE idle:** `sceGuSetMatrix` 0.54 us, texscale shim 0.19 us,
  `sceGuDisable` 0.13 us, `sceGeListUpdateStallAddr` 0.54 us, an uncached
  list store about 19 ns. sceGu was about 4 us of a submit, not most of it.
- **Result, fight blocks:** `fl_clay_draw` per drawn step went down by
  0.1-0.8 ms (for example 12.50 -> 11.70 and 13.39 -> 12.56). GU submit went
  down by 0.3-0.8 ms (7.99 -> 7.37 at 168 draws).
- **Tried and dropped:** writing the words through cached lines and
  `cache 0x1B` (user mode works) was slower than uncached stores.

Mesh submit cost by removal (P107, 2026-11-23, PSP-2003, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`):
- **Why by removal:** the `PROFILE=2` timers add about 4 ms to `fl_clay_draw`
  (13.35 against 9.2-9.6 ms), so its split is unreliable. Each stage was
  measured by returning early from `r_gu_mesh_draw` instead. Fight blocks,
  about 150 submits a frame.
- **`fl_clay_draw` totals:** 9.61 ms as built. 4.78 ms with no submits at
  all. 5.00 ms returning after the flush and canvas. 5.90 ms after the
  texture part. 6.56 ms after the blend memo and depth zoom. 7.46 ms with
  every word emitted but zero-vertex draws.
- **Per submit:** flush 1.5 us, texture 6 us, blend/depth zoom 4.4 us,
  emitting the words 6 us.
- **The rest is the GE drawing:** drawing the meshes costs the CPU 2.15 ms
  inside `fl_clay_draw` and 4.9 ms over the render pass (23.00 against
  18.13 ms). That is bus contention, not submit overhead.
- **Probes:** untextured meshes were no cheaper (the probe rebinds every
  draw, so it is inconclusive). `MVC2_NO_VRAM_TEX=1` adds 1 ms. Copying each
  draw's vertices into a VRAM ring costs more than it saves (13.1 ms).

Mesh vertices in VRAM (P108, 2026-11-23, PSP-2003, `fight6`, `PROFILE=1`):
- **Probes, render pass per drawn fight step:** 22.57 ms as built; 21.65 ms
  with mesh textures read from VRAM; 21.67 ms with every index pointing at
  vertex 0, which means no pixels and so no texture reads either; 18.05 ms
  with zero-vertex draws. So texture reads cost about 0.9 ms. The rest,
  about 3.6 ms, is the GE fetching a 24-byte vertex from main RAM for every
  index. Drawing pixels costs the CPU nothing.
- **The change:** unlit meshes drawn from `gv` get a copy in a pool at the
  top of VRAM, and the GE reads that copy. The copy is refreshed when `gv`
  is rewritten, only on the path where `gv` is not yet in this frame's list.
  The CPU keeps reading `gv`. The pool follows the P88 texture-cache policy:
  a full pool is kept while the frame used at least half of it, and it is
  reset otherwise or for a system dialog. `MVC2_MESH_VRAM_KB` sets its size
  (default 256 KB; 0 = off). It comes out of the texture cache, which drops
  from about 690 to 434 KB.
- **Pool sizes, render pass:** off 22.40 ms, 128 KB 21.11 ms, 256 KB
  19.93 ms, 384 KB 19.86 ms (384 KB was worse in character select). About
  105 draws a frame read from VRAM, and about 1 KB a frame is copied.
- **Repeat run:** render pass 22.28 -> 19.91 ms; `fl_clay_draw`
  8.78 -> 7.82 ms.
- **Normal mode, fight display rate (fps):** 11.99 -> 12.57, 11.99 -> 11.99,
  18.73 -> 23.58, 9.99 -> 11.79, 8.99 -> 9.83.
- **PPSSPP:** shots f1-f6 are byte-identical to the build before P107, with
  the pool on and off. Check that a PPSSPP launch actually ran: some
  launches exit without running the game, leaving the previous run's shots
  and log in place.

Mesh submit cuts (P107, 2026-11-23, PSP-2003, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`, two runs each):
- **Counts, per fight frame (about 144 submits):** about 97 reuse the
  previous submit's texture, and 47 bind a texture. About 35-40 are a
  mesh's next range with the same texture and contiguous indices. Only
  about 7 read indices or vertices from uncached list memory.
- **Kept:** in `clay_draw`, ranges that follow on with the same texture are
  merged into one submit. Their colours are already in the vertices.
  `MVC2_MESH_NOMERGE=1` turns it off. Shots f1-f6 are byte-identical to
  HEAD. `fl_clay_draw` went 9.18 -> 8.82 ms, but the render pass (22.42 ->
  22.37) and the frame time are unchanged: the time moves to the rest of the
  pass.
- **Dropped:** a texture fast path (skip the record reads when the texture
  is the previous submit's) and skipping a CLUT reload when the same CLUT is
  loaded. Together with the merge they were slower than the merge alone
  (`fl_clay_draw` 9.08-9.15 ms).
- **Shots and env:** shots taken with `MVC2_STATE_DUMP` set differ from
  shots without it. Compare shots only between runs with the same env.
- **What paces the frame:** the GE's main-RAM traffic while meshes draw (see
  above), not submit CPU work (P108).

ADC box test and mesh indices (P83, 2026-11-23, PSP-2003, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`, two runs each):
- **ADC by removal:** `MVC2_NO_ADC=1` saves 1.4 ms. About 66 draws a frame
  reach the ADC, and the bounding-box test passes for all but about 2. The
  box test alone cost 0.9 ms. Only 1 box a frame is recomputed, and the
  per-vertex test and the index filter cost under 0.2 ms.
- **Why the box test was slow:** in a tight loop mid-fight it took 4.8 us
  a call. A 4x4 `fl_mat_mul` took 0.87 us, and the C corner path took 8.9 us.
  The corners went from CPU stores to VFPU loads and back, and scalar FPU
  code is slow on this CPU. Caching the result per mesh hit only 4 of 66
  draws, because the camera moves.
- **The change:** `vfpu_bbox_inside` (`psp_vfpu.S`) runs the same corner
  test in VFPU registers with no stores, in 0.86 us a call. Corners are
  P + edge sums, the margin uses a bound >= the C path's, and it returns 0
  for a non-finite matrix. A temporary check found 0 disagreements with the old test
  over 109,568 draws.
- **Indices in VRAM:** the P108 pool also holds each mesh's indices, copied
  once, because they never change. The GE reads them from VRAM when the ADC
  filtered nothing. `MVC2_MESH_VRAM_NOIDX=1` turns it off. The render pass
  went 19.89 -> 19.66 ms.
- **Result:** `fl_clay_draw` 7.85 -> 6.93-6.95 ms; render pass 19.94 ->
  18.59-18.66 ms. In normal mode, fight display fps went from 12.57, 11.99,
  23.58, 11.79, 9.83 (P108) to 14.98, 14.98, 27.16, 11.99, 10.47. PPSSPP
  shots f1-f6 are byte-identical to HEAD.
- **Left:** about 2.9 ms of submit CPU (P109), and about 4 ms of per-draw
  work before submit (P110). `fl_clay_draw` <= 4 ms needs both.
- **Measuring:** with temporary counters in, runs varied by about +-0.2 ms, not
  0.05. Use A/B pairs in the same build and repeat each one.

Mesh submit cost and swizzled textures (P109, 2026-11-23, PSP-2003, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`, two runs each):
- **A submit's own CPU work is small:** writing the projection words twice
  per submit adds 0.04 ms a frame, and binding each texture twice adds
  0.07 ms. Skipping the depth-zoom sampling saves 0.2 ms; the texscale
  divides save nothing. Skipping texture binds or projection words looks
  like -1 ms, but it changes what the GE draws. So most of P107's ~18 us
  per submit is the GE's main-RAM traffic slowing the CPU, not submit code.
- **Where the GE reads RAM:** about 33 texture binds a fight frame miss the
  VRAM cache (~870 KB of texture size, mostly IDX8, ~280 KB 8888); 14 hit.
  New `PROFILE` counters: `mesh tex RAM binds`, `mesh tex RAM KB bound`.
- **The change:** textures (when the padded height is a multiple of 8) and
  the character atlas pages are stored swizzled: 16-byte x 8-row blocks,
  which the GE fetches in far fewer bus transactions. `tex_copy` and the
  tile decoder write the swizzled layout, and the binds set the swizzle bit.
  Sampling is the same. `MVC2_NO_SWIZZLE=1` stores them row-linear.
- **Result against HEAD:** render pass 18.59 -> 17.86-17.88 ms,
  `fl_clay_draw` 6.93 -> 6.88-6.90 ms. In normal mode, fight display fps
  went from 14.98, 14.98, 27.16, 11.99, 10.47 to 14.98, 14.98, 22-27.6,
  14.98, 11.4-12.0. Block 3 varies that much between runs. PPSSPP shots
  f1-f6 are byte-identical to HEAD.
- **Mesh pool size:** with swizzling, 192 KB is the same as 256 KB within
  noise, and 320 KB is worse (18.13 ms). It stays at 256 KB.
- **Layout traps:** adding an `int` to `TexRec` (48 -> 52 bytes) cost about
  0.5 ms, and probe code in the submit path shifted results by 0.3-0.4 ms.
  Measure A/B pairs within one build, and re-measure HEAD on the same day.
- **Left:** the rest of the RAM traffic (P111) and the per-draw work before
  submit (P110).

GE main-RAM traffic (P111, 2026-11-24, PSP-2003, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`, two runs each; closed, no lossless cut):
- **Textures:** about 33 binds a fight frame read from RAM. Pointing them at
  VRAM (same reads, wrong pixels) took the render pass from 18.10 to
  17.18 ms, so they cost about 0.9 ms. Every fight texture is IDX8 and uses
  all 256 palette entries, so none can become T4. The working set is about
  1.1 MB against a 434 KB cache.
- **VRAM choice:** one 512x512 texture (256 KB, 8 binds a frame) holds 60%
  of the cache. A policy that keeps the most binds per KB moved binds from
  33 RAM / 14 VRAM to 12 / 34, but the pass went 18.31 -> 18.45 ms: big,
  rarely bound textures are read the most. Keeping textures over 128 KB out
  measured -0.25 ms in this one scene, too little to generalise.
- **Other readers:** 105 of 107 `gv` draws already read the P108 pool, and
  lit draws are about 0 a frame. Character atlas pages from VRAM: -0.05 ms.
  CLUT loads are about 47 KB a frame. A 160 KB mesh pool (more texture
  space) gave the same pass as 256 KB; 128 KB was 0.6 ms worse.
- **Left:** cutting texture reads means lossy copies (P105).

Half-size texture copies (P105, 2026-11-25, PSP-2003, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`, one build, two runs each; closed, not worth it):
- **Ceiling:** a temporary switch pointed every mesh texture bind that reads
  RAM (cache full) at VRAM (same sizes, wrong pixels): render pass
  14.57 / 14.54 -> 13.91 / 13.90 ms, `fl_clay_draw` 3.33 / 3.36 -> 3.19 ms.
  Any reduced copy saves at most these 0.64 ms (4%), less the cost of making
  the copies, and blurs the 3D scene; not done.

Per-draw mesh work (P110, 2026-11-24, PSP-2003, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`, two runs each; partial):
- **Doubling each stage** (the extra cost of running it twice, `fl_clay_draw`):
  the timer reads +0.20 ms, `r_gu_begin` +0.19, the matrix chain +0.38, the
  ADC +0.14, clip prep +0.41. 41 of about 107 draws a frame use the clipping
  programs, and only 5 of those need `near_clip`.
- **The changes:** `vfpu_mesh_mats` does world * view * projection * screen
  and the ADC matrix in one call, with intermediates in registers. It runs
  the same `vtfm4` rows as chained `vfpu_mat_mul`, and was bit-identical
  over 299,008 draws. `vfpu_clipbox_inside` is the clip box test in
  registers, as P83's. `vfpu_clip_any` is a registers-only pass that skips
  `clip_prepare` when no vertex would get an outcode. Over 186,368 draws
  both agreed with the old code every time. `fl_clay_draw` reads the timer
  only with `DRAWLOG`.
- **Result against HEAD:** `fl_clay_draw` 6.89 -> 6.25-6.27 ms, render pass
  17.88 -> 17.26-17.30 ms. In normal mode, fight display fps was 15.98,
  14.98, 28.54, 14.98, 10.81. PPSSPP shots f1-f6 are byte-identical.
- **Layout (P112):** giving `clay_draw` a second call site stopped it being
  inlined and moved about 17 KB out of the P101 hot block, which cost about
  1 ms. Moving the VFPU routines into or after the hot block cost 0.5-1.2 ms
  of render pass, and an 8 KB align did not fix it. Within one build the
  P110 changes were worth 0.85 ms. Check `llvm-nm -n` that the hot block's
  end moved only by what you changed.
- **Doubling overstates:** within one build, the summed doubling costs above
  were about 1.3 ms, and the cuts saved 0.85 ms.
- **Split by removal (2026-11-24, after P112, early return at each stage,
  `fl_clay_draw` 6.53 ms as built):** entry and `r_gu_begin` 0.65, colours
  and vertex copy 0.45, matrices 0.58, ADC 0.54, clip prep 0.44, range loop
  with `near_clip` (5 draws, ~76 us each) 0.83, flush/canvas 0.07, texture
  key and bind 1.11 (the 47 binds ~0.5), blend/depth key 0.29, depth zoom
  0.27, emitting the draws with the GE drawing ~1.4 (submit work alone
  without draws 5.18). No stage is over ~1.1 ms: 4 ms needs most of them
  cut, or the work off the main CPU (P103/P104).
- **Tried, no gain:** writing list-memory bulk data (lit and stale vertex
  copies, ADC index lists, `near_clip` output) through the cached alias
  with a write-back instead of the uncached mirror: 6.57 ms.

Recorded mesh submits (P103, 2026-11-24, PSP-2003, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`):
- **How:** `r_gu_mesh_draw` records each submit (clip matrix copied, the
  blend/depth/`state_ad0` it reads, pointers, UV scale) and `rec_build`
  writes the GE words with the old emit code (`mesh_emit`), the item's
  state put back into R, before anything else writes to the list: a 2D
  batch, a clear, a fill, a scissor change, a texture or CLUT about to
  change (`in_use_sync`), a list close. Fights have ~115 submits in ~2 runs
  a frame and no texture syncs inside a run (PPSSPP count).
- **Switches:** `MVC2_REC_OFF=1` emits at the call (A/B only; it measures
  1.3 ms slower than HEAD in this layout). `MVC2_REC_CHECK=1` hashes each
  item's indices and vertices at record and build: 229,390 items, none
  changed. Shots f1-f6 byte-identical on, off and checked (PPSSPP).
- **Result (two runs each, `hot_order.ld` regenerated):** render pass
  17.33 (HEAD, same session) -> 16.73-16.74 ms; `fl_clay_draw` 6.28 ->
  4.80-4.82 (the submit work now runs in `rec_build`). With the old hot
  order the recorded path measured 16.44-16.45.
- **Not covered:** 2D batches are still written at the call. The build
  runs in the same frame as the record; a one-frame-late build (P104)
  needs the referenced vertex copies, `gv` and textures kept a frame.

Media Engine spike (P114, 2026-11-24, PSP-2003 ARK-4, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`, one run each):
- **Boot:** `psp_me.c` (`ME_SPIKE=1..3` in env.txt) copies `psp_me_stub.S`
  to the ME reset vector 0xBFC00000 through kubridge `kuKernelCall`,
  leaves entry/stack/argument at 0xBFC00600 and pulses bits 0x14 of
  0xBC10004C. The stub sets the ME bus clocks (0xBC100050 |= 0xF), Status
  0x30000000, drops the ME caches and jumps to C in kseg0 with a stack in
  main RAM. Shared words: main CPU through 0x40000000, ME through
  0xA0000000. The ME code must be o32 like the rest (an EABI unit calling
  o32 libc crashed: o32 callees write the caller's 0..15($sp)).
- **Main CPU cost of ME memory traffic** (render pass / `fl_clay_draw`):
  ME idle 17.63 / 6.38 ms; 512 KB copied per logic step (~30 MB/s, a
  list-build stand-in, always finished in the step) 17.88 / 6.63 (+0.25);
  copying flat out (~100-150 MB/s) 19.88 / 7.61 (+2.25).
- **Sleep and the XMB (P115, 2026-11-25, PSP-2003 ARK-4, packed EBOOT from
  the memory stick, `ME_SPIKE=3` and `MVC2_ME_BUILD=1`):** HOME -> Exit to
  the XMB works in both (spike: ME still copying at exit; build path: ME
  parked). Sleep from a fight crashes in both: screen off, green light stays
  on, then the PSP powers off; nothing reaches the log. With an empty
  `env.txt` (ME untouched) sleep, wake and the quit all work (`[plat]
  suspend`/`resume` logged). P117: give the ME back to the firmware before
  sleep.
- **Cold give-back (P117, 2026-11-25, same setup):** while our code owns the
  ME, `scePowerLock(0)` is held; the power switch (`[power] flags 8000xxxx`)
  makes `r_gu_me_poll` park our code, put back the 0x800 bytes of ME RAM
  saved at take, set the firmware's boot words (0x70C = 0x01FF01FF, 0x714 =
  2, state 0x700 = -3) and release reset; the firmware's code reports -4
  (started), then the power lock is dropped. Over PSPLink
  (`MVC2_ME_GIVEBACK_AT=frame`, no sleep) the ME comes back and is taken
  again after 3 s mid-fight. Real sleep: suspending starts (`000110e4`), then
  the same power-off. A cold restart of the firmware's ME code is not enough;
  its sleep needs the state it had before we took it (halt it with its own
  sleep-save call at take, resume it from that at give-back).
- **Power events seen:** switch `800000e4`, suspending `000100e4`, resume
  complete `000400e4` (bit 0x1000 set on AC power). The log is kept as
  `mvc2_psp.prev.log` at each start.

Audio mix cost (P106, 2026-11-25, PSP-2003, `fight6`, one run each):
- **How:** the `[audio]` line (every 300 logic frames) now also gives the
  fill time per 60 Hz frame: the summed wall time of the audio thread's
  fills (`device_fill`, priority 0x12, above the game thread) over the wall
  time since the last line. Profile timers already leave this time out (P91),
  so render pass figures never included it.
- **Result:** 1024-sample fills at ~47 a second. Fight blocks in normal mode
  (no PROFILE): 1.28, 1.22, 1.79, 1.63, 1.85 ms per frame (mean 1.55);
  character select and VERSUS 1.8-2.0; menus 0.8. With `PROFILE=1
  NO_FRAME_SKIP=1` the same within 0.1 ms (1.20-1.75).
- **Not covered:** a fill waiting on `audio_lock` while the game thread
  holds it counts as fill time, so this is an upper bound on what the mix
  takes from the main CPU; no split into decode and mix.

Per-draw code footprint (P110, 2026-11-24, PSP-2003, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`, two runs each):
- **Split by removal (after P116):** `fl_clay_draw` 4.48 ms over ~102 draws:
  entry + `r_gu_begin` 0.76, colours/vertex copy 0.54, matrices 0.71, ADC
  0.50, clip prep 0.55, range loop 0.54, recording the submits 0.88 (with the
  GE's bus traffic). 5-7 us per stage per draw whatever the arithmetic:
  I-cache misses (the game's packet build runs between draws). `rec_build`
  is 0.66 ms a frame (~109 items, ~3.5 runs, never at `REC_MAX`).
- **Change:** rare paths out of the per-draw code (`COLD`: noinline, cold):
  viewport rebuild, scalar fallbacks, `MESH_DUMP`, list-memory vertex
  copies, colours, positions, `near_clip`, `bbox_corners`, `me_check`;
  `fl_clay_draw` 14.7 -> 5.5 KB. `near_clip` copies the mesh's vertices once
  per draw and keeps unclipped triangles' indices (it de-indexed every
  triangle of a clipped range); the same triangles reach the GE in order.
- **Result:** `fl_clay_draw` 4.40-4.64 -> 3.87 (with a dead env switch still
  in) / 4.15 (final), render pass 16.2 -> 15.27 / 15.67-15.70; normal mode
  fight fps 19.98 19.98 29.00 14.98 13.76. PPSSPP shots f1-f6 identical.
  Removing the dead switch moved the layout by ~0.3 ms (P112).

Per-draw path trimmed (P110, 2026-11-25, PSP-2003, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`, `hot_order.ld` regenerated, two runs):
- **Change:** `r_gu_begin` and `r_gu_mesh_draw` skip `r_flush` + `canvas_bind`
  when nothing is pending (`R_READY`); rare branches in `clay_draw` marked
  `RARE` (`__builtin_expect`, laid out after the per-draw path) and the
  scalar box tests and `clip_prepare` made `COLD` (`fl_clay_draw` 8.5 ->
  5.1 KB); the viewport/canvas key is read only when `r_gu_vp_gen` changes;
  small `memcmp`s inline.
- **ADC memo:** when the ADC matrix and the vertices are bit-identical to the
  mesh's last ADC, its result (kept indices in a per-mesh cached buffer,
  written back, or "keep all") is reused; ~30 of ~68 ADC draws a frame hit
  (PPSSPP, `PROFILE=2` slots 62/63). The buffer is not rewritten after the GE
  was handed it in the same frame (list memory then, as before).
- **Result:** `fl_clay_draw` 4.15 -> 3.37 / 3.39 ms, render pass 15.67-15.70
  -> 14.72 / 14.70 ms. PPSSPP shots f1-f6 identical; hardware shots not compared.

Mesh words on the Media Engine (P104 design (a), 2026-11-24, PSP-2003, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`, one build, one run each):
- **How:** `rec_build` resolves per item what needs main-CPU state (texture
  key, `tex_addr` VRAM copies, in-use gens, depth zoom) into a `MeItem`;
  `gume_build` (`r_gu_me.c`, no libgu/libc/static data) writes all the mesh
  words (bind, CLUT, blend/depth, matrices, draws) into a block reserved in
  the list that the list CALLs. `MVC2_ME_BUILD=1`: the ME builds it, the
  stall held before the block's JUMP until done (`__wrap_sceGeListUpdateStallAddr`),
  `list_close` waits; `=2`: the main CPU builds it; `MVC2_ME_CHECK=1`: every
  item against sceGu's words (295k items, 0 differ; PPSSPP shots f1-f6
  identical in all modes; hardware f1-f3 identical, f4-f6 differ run to run
  even within one mode). PPSSPP does not run our ME code (falls back to `=2`).
- **Result, render pass / `fl_clay_draw`:** default 16.64 / 4.70; ME 16.24 /
  4.68; main CPU 16.12-16.18 / 4.63-4.64. The gain is the writer, not the ME;
  the ME never made `list_close` wait, GE held ~7 times a frame.
- **Exit:** a PSPLink `reset` leaves the ME running our code, which then
  corrupts the next program; `r_gu_me_stop` (end of main, exit callback)
  parks it in a loop in its reset-vector memory (`psp_me_park`).
- **Bug found on the way:** a block one word short (bind is 16 words)
  lost its RET to the CALL after it; the GE re-CALLed it forever (hung only
  with `=2`). `=2` now aborts on an overflow.
- **Default since P116:** `=2` (main CPU) with `hot_order.ld` regenerated;
  `MVC2_ME_BUILD=0` restores `mesh_emit`. Two runs each: render pass 16.56-16.59
  -> 16.20-16.24 ms, `fl_clay_draw` 4.70 -> 4.59-4.64. Normal mode, fight
  display fps by block: 19.98 19.98 28.50 14.98 12.99 -> 19.98 19.98 28.97
  14.98 14.05 (one run each).

Round-start blink (P113, 2026-11-24, PSP-2003 and PPSSPP, `fight6`, `NO_FRAME_SKIP=1`):
- **What was seen:** now and then a held frame with only the sky and a few
  stage meshes; no rest of the ship, characters or HUD.
- **Cause, the game's own:** `func_0016CFB0`/`func_0016CFE0` count
  D_004F1CB8 down and set D_004F1CB0 to its bit 0 or bit 1; while it is 1,
  `func_001529A0` draws only layers 0xE/0x12 (no stage layers 5/7, no
  active objects). Steps 1812-1822 alternate ~116 and ~46 draws, the same
  counts in PPSSPP. At 60 fps it is a blink; at 5-15 fps a blink frame
  stays up for 100+ ms. Nothing to fix in the renderer.

Stable code layout (P112, 2026-11-24, PSP-2003, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`):
- **What moved timings:** padding inserted after the P101 hot block shifts
  all other code and data. Fight render pass by padding: 0 KB 17.25 ms,
  1 KB 18.25, 2 KB 17.72, 4 KB 17.47, 6 KB 17.39, 8 KB 17.25. The 8 KB
  period is the cache way size. Shifting only the data by 1 KB cost
  +0.26 ms. Code not in the hot list is sorted by name, so a new function
  named `p...` moved every per-draw helper after it (`psp_mesh_predraw`,
  `r_*`).
- **The rule now:** `hot_order.ld` lists every function the HOTPROF run
  called (2,127, 685 KB), ordered by calls. libpspgu, libc
  `memcpy`/`memset`/`memcmp` and `psp_vfpu.o` (not instrumented) follow the
  first 96 KB, where they sat before. `.rodata`, `.data` and `.bss` each
  start at a fixed offset in an 8 KB period (`ALIGN(0x2000) + phase`, which
  costs about 24 KB). `hot_order.py` writes all of this; regenerate after
  big changes to called code.
- **Check:** an unused 2 KB function moves the render pass by 0.04 ms
  (before: 0.19 ms, and 0.5-1 ms when it shifted the code after the hot
  block). A 2 KB shift after the first 96 KB moves it by about 0.1 ms.
  Render pass 17.35-17.40 ms, about 0.1 ms above the old layout's lucky
  phase. PPSSPP shots f1-f6 are byte-identical.
- **Not covered:** a size change in a called function still moves the
  functions ranked after it, by about 0.1 ms. `.sceStub.text` (syscall
  stubs) still follows `.text`.

Hot routines first in .text (P101, 2026-11-23, PSP-2003, `fight6`, `PROFILE=1`):
- **How it works:** every o32 TU is built with `-ffunction-sections`, and
  `port/psp/hot_order.ld` (`ld --section-ordering-file`) puts the 171
  most-called functions (96 KB) first in `.text`.
- **Regenerating it:** configure a second build with `-DMVC2_PSP_HOTPROF=ON`.
  That adds `-finstrument-functions-after-inlining` and `host/psp_hot.c`,
  which writes `hot.txt` at exit. Run `fight6` in PPSSPP (call counts don't
  depend on timing), then:

      python3 port/psp/tools/hot_order.py build/port-psp-hot/mvc2_psp hot.txt -o port/psp/hot_order.ld

  (P112: the default budget now takes every called function; see below.)

- **Result (two runs each, A/B against the build before P101):**
  render pass 11.0 -> 10.6 ms a step; game update 1.43 -> 1.38 ms.
  Character select update 20.0-20.2 -> 19.0 ms and 18.6 -> 16.8 ms. Fights
  are within noise.
- **Budgets:** 32 KB gave about half the gain; 96 KB is kept.

Frame skip (P100, 2026-11-23, PSP-2003, `fight6`):
- **The problem:** when it falls behind, the platform runs up to 8 logic
  steps per display frame and shows only the last. The PSP renderer drew all
  of them, so fights ran their logic at half speed (33-37 steps/s) and showed
  4-5 frames a second.
- **The fix:** `r_gu.c` `r_set_discard` drops the GE work of steps that
  won't be shown, as upstream `r_gl.c` does. The logic still runs.
- **Result:** fight logic 59.94 steps/s, display 10-12 fps. Character select:
  logic 53-60, display 6.7-7.5 (was 3.5-3.9).
- **Profiles:** a `PROFILE` run now averages drawn and undrawn steps, so
  per-step render figures fall. `NO_FRAME_SKIP=1` draws every step, for
  figures comparable with the tables above.
- The `[plat]` line now also prints the display rate and the steps drawn but
  not shown.

Packet build (P84, 2026-11-23, PSP-2003, `fight6`, `PROFILE=1`):
- **The cost:** in a fight frame the texture memo check (`psp_fe_sync.c`,
  P85) runs about 29 times, the first time each handle appears that frame.
  `check_fixed` cost about 73 us each, 2.1 ms a frame: it hashed the two
  64-byte descriptors and up to 1 KB of palette one byte at a time, and each
  step waited on the last multiply. The strided texture samples
  (`check_part`) cost 1.3 ms, mostly cache misses.
- **The fix:** `hash_words` covers the same bytes a word at a time, in two
  chains.
- **Result:** packet build in the 3v3 blocks went 4.7-5.0 -> 3.7-3.8 ms. The
  screenshot is identical.

Non-mesh rendering split (P91, 2026-11-23, PSP-2003, `fight6`, `PROFILE=1`):
- **How it's measured:** `psp_prof_scene.c` times each step of the two render
  tasks, minus the mesh draws (`func_003F0500`) made inside it. All
  `psp_prof.c` timers now leave out the audio thread's fills, which preempt
  the main thread for about 1.3-2.5 ms a frame (`psp_audio_busy_us`).
- **Fights (`func_001529A0`):** render pass 27.3 ms, mesh draw 18.9 ms, so
  about 8.4 ms of non-mesh rendering, spread over nine steps.
  - `func_00153250` 1.9 ms
  - `func_00153710(0xE)` 1.9 ms
  - `func_00153310` 1.2 ms
  - `func_00152AD0` 1.0 ms
  - `func_00153A40` 0.9 ms
  - the others under 0.8 ms each

  No part reaches 2 ms.
- **Character select (`func_00152570`):** render pass 32 ms, mesh draw
  17.7 ms, so about 14.3 ms of non-mesh rendering. Most of it is object list
  6 (`D_004D3690[6]`):
  - draw, `func_001520E0(6)`: 8.3 ms (P98)
  - walk, `func_00151A20(6)` -> `func_00151A90`: 5.5 ms (P99)

Model node draw (P98, 2026-11-25, PSP-2003, `fight6`, `PROFILE=1 NO_FRAME_SKIP=1`, `hot_order.ld` regenerated, one build, two runs each against `MVC2_NODE_GAME=1`):
- **Split by removal:** per node (~168 a select frame with every character
  unlocked) `func_001538B0` multiplies the node matrix, sets state 0x19,
  then `func_001537D0` (blend states 0xD/0x5D through `func_00153A40`, and
  the UV matrix 0x1A, which the game clears after each mesh) and the mesh.
  Skipping `func_001537D0` saved 2.3 of 5.3 ms, the matrix product 0.35,
  the 0x19 set 0.37; skipping the mesh call also cut 2.2 ms of non-mesh
  time (cache misses around the mesh draw).
- **Change (`psp_node_draw.c`, wraps):** the node loop with `func_001537D0`
  inlined; blend states skipped when the game's mode word holds them and the
  renderer is in sync (~94% of nodes); matrix sets are the game's copy and set
  bit, plus the renderer's copy only when it does not read the game's (P96);
  the zero-angle UV rotation made once; 64-byte copies inline.
- **Result, `func_001520E0(6)` non-mesh per select frame:** all characters
  unlocked 5.58-5.62 -> 3.93-4.06 ms (render pass 19.1 -> 17.6); `unlock=stock`
  (fewer models, ~96 meshes) 3.32 -> 2.55-2.59. Fights: render pass
  14.47-14.51 -> 13.81. PPSSPP: shots f1-f6 and select-screen shots identical
  to `MVC2_NODE_GAME=1`; oracle state dump 3,057 frames, 0 words diverge.
- **Measuring:** `PROFILE=1` blocks are 300 frames; how much of the select
  screen falls in them depends on load timing and the unlock setting. The
  new `select: frames drawn` line (1.00 = every frame) marks full blocks.
- **Second pass (2026-11-25, `unlock=all`, `hot_order.ld` regenerated each
  time, two runs):** each select object draws one node and one mesh (~165 a
  frame), so the cost is per object. `func_00152400` (object colour) is
  PSP-side: its four soft-float `fptoui` calls become FPU truncations (the
  game's for NaN, inf, >= 2^32) and the 0x66 set is skipped when it changes
  nothing (97%). `psp_mesh_predraw`'s dump, log and set-up paths are out of
  line (4.7 KB -> 356 bytes). Non-mesh 3.84-3.88 -> 3.19-3.21 ms, select
  render pass 16.8 -> 16.0-16.2, fights 13.16/13.76/12.88/15.40 ->
  12.71/13.25/12.46/14.79. PPSSPP: shots f1-f6 identical to `MVC2_NODE_GAME=1`,
  per-frame records 3,057 frames identical. Snapshots (frames 600-3000):
  every diverging word is past the EE heap end (0x01D34380, host memory:
  sound-bank headers at frame 600) except 0x004C2A40, which also differs
  between two runs of the same build (checked 2026-11-25).
- **Tried and dropped:** a two-pass node loop (matrices first: non-mesh
  -0.13, mesh draws +2.5 ms), func_00152180 merged PSP-side, renderer mode
  words read in place, the object-draw chain first in `hot_order.ld`. The
  function layout alone moves the non-mesh figure by +-0.4 ms. The timers
  cost ~1.1 us a read per mesh (~0.2 ms of the figure).
- **ZBUF packets (2026-11-25, `psp_packet.c`):** `func_003E0530` (states
  0x6B/0x6C; 34 select objects a frame set and reset 0x6B) writes the game's
  ZBUF copy `D_005BF2D0` (and `D_0062BFF0/8` for mode 2) and leaves out the
  GS packet and its `D_0062BBA0` queue entry, as P95. `MVC2_ZBUF_FULL=1`: the
  game's body. PSP-2003, `unlock=all`, same EBOOT, two runs each: non-mesh
  3.19-3.26 -> 2.80-2.93 ms, select render pass 15.9-16.0 -> 15.4-15.6, fight
  render pass 13.61 -> 13.41-13.47. That is with the `hot_order.ld` of
  8c44ce83f; one regenerated with the host under load placed worse (3.63 ->
  3.34-3.44 ms, same A/B gain) and was not kept. PPSSPP: shots f1-f6 and
  3,057 per-frame records identical; snapshots differ only in the GS packet
  arena, past the EE heap end, the two DMA queue ring halves at 0x0173C5B0
  and 0x0173F5B0 (frame 600, entries left out) and 0x004C2A40 (run noise).

Object matrix walk (P99, 2026-11-26, `psp_walk.c`, PSP-2003, `fight6`, `unlock=all`, `PROFILE=1 NO_FRAME_SKIP=1`, same EBOOT A/B against `MVC2_WALK_GAME=1`, two runs each):
- **Change:** `func_00151A90` (each object's matrix, ~165 a select frame;
  most take the rotation path, many the scale and 0xC8 matrix) is PSP-side
  with the game's float expressions. The three rotation angles (four fp-bit
  double calls each) and their sin and cos (`func_003DE720`) come from a
  memo keyed by the u16 and the FPU rounding mode, filled by the game's own
  calls; 4x4 products, identities, translations and the three rotations are
  inline. `MVC2_WALK_CHECK=1` runs both bodies per call and counts
  differences.
- **Result, `func_00151A20(6)` per select frame:** 5.25-5.35 -> 1.21-1.32 ms
  (angle memo alone 2.31-2.36, plus inline matrices 2.05-2.09); select
  render pass 15.7 -> 11.6-11.8, fights 13.57 -> 13.19-13.27. In this layout
  P98's draw figure is 2.98-2.99 ms (3.07-3.08 with the game's walk).
- **PPSSPP:** check mode 245,760 calls, 0 differ (`unlock=all`); shots f1-f6
  and 3,057 per-frame records identical; snapshots differ only past the EE
  heap end and at 0x004C2A40 (run noise).

Tex select upload path (P95, 2026-11-23, `psp_reslist.c` `psp_texsel`):
- **What changed:** for lists with a texture that isn't resident, the PSP runs
  the game's residency bookkeeping call for call (lookup, GS block allocation
  with its retry, insert). It leaves out the GS upload packets and the
  D_0062BBA0 DMA queue, which the PSP renderer never reads.
  `MVC2_TEXSEL_FULL=1` runs the game's own body.
- **PSP-2003 (`fight6`, `PROFILE=1`, A/B):** tex select went 0.90 -> 0.83 ms
  on average, and 1.54 -> 1.28 ms at most. About 0.44 ms of each figure is the
  profiler: about 220 calls a frame with 2 timer reads each. That makes the
  real cost about 0.46 -> 0.39 ms, and about 0.84 ms in the heaviest block.
  The `fight6` screenshots are byte-identical.
- **Game state (PPSSPP, 6,000 headless frames):** the per-frame records match.
  The snapshot differs only inside the GS packet arena (EE 0x0163A3C0 to
  0x0173A3C0, two 512 KB halves), plus the known word past the PSP heap.

Audio (P92, fight6 script, `[audio]` fill line, ms per 1024-frame fill):
mean 2.96 -> 1.53 over the run, heaviest section 6.1 -> 2.1, worst 19.1 ->
5.9. The mixer is fixed point with linear interpolation (mix_psp.c,
audio_psp.c): on the Allegrex, cost follows instructions per sample (~2.5
CPI), not float vs int, so steady voices mix in tight runs per decoded block.

VFPU (P88, P93; psp_vfpu.S, fight6, PROFILE=2, MVC2_NO_VFPU A/B):
fl_clay_draw 17.6 -> 17.0 ms; clip_prepare 1.72 -> 1.25, matrices + ADC
4.9 -> 4.4. Mesh cost is per draw (~115 draws, ~15 us each), not vertex
math; fights have no lit draws.

## Real hardware (P82)

Verified on a PSP-2003 with ARK-4. PPSSPP accepts all of these; hardware does not:
- The game links as a relocatable PRX (`MVC2_PSP_PRX`, default ON): a static
  ELF at 0x08804000 fails to load (800200D9).
- `-mno-check-zero-division` (psp-clang.cmake): clang's `teq` divide guard is
  not an Allegrex instruction (reserved-instruction exception, PSP freezes).
- FPU exception enables are cleared (psp_fpu.c, audio thread): PSP threads
  start with invalid/divide/overflow traps on.
- Packed EBOOTs carry `/SLUS_204.86` (EE data filled from it); zeroed data
  reads near address 0, which PPSSPP tolerates.
- Under ARK-4 any CPU exception freezes the PSP (then it powers off), with no
  error. The log closes after every line, so it ends at the real stop.

Debugging on hardware: put `port/psp/exc/mvc2exc.prx` (`make -C port/psp/exc`)
next to the EBOOT to get a 1 s watchdog of the main thread and on-screen log;
`CRASHTEST=1` in env.txt tests the reporter. `-DMVC2_PSP_CRUMBS=ON` logs entry
to the functions listed in `port/psp/debug/psp_crumbs.S`. `SCREENLOG=1` shows
the boot log on screen without the module.

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

- Primitives are scaled straight into the 480x272 back buffer (P23). Default
  **fit**: the game's displayed area (640x448) with its aspect kept (389x272,
  centred; HUD text readable over a full arcade run). **stretch** fills
  480x272. `ms0:/PSP/GAME/mvc2/settings.txt` (written with the defaults on
  first run): `screen=fit|stretch`, `color=32|16`; L+R+Select in game swaps
  the screen mode and saves it (the game does not see that press).
  `color=16`: dithered 5650 draw/display buffers, half the VRAM (557 KB freed)
  and fill bandwidth; read at start. Default 32, as 16 can band gradients;
  its speed-up is on hardware only (PPSSPP does not time the GE).
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
on first use into 512x512 T4 pages in RAM (128 KB, at most 4 per bank (P35); a full
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
`SCRIPT=path` in `env.txt` (testing) runs upstream's state-driven input scripts
(`port/tools/scripts/`, `psp_script.c`); with `MVC2_CPU_HP=1` and
`MVC2_P1_HP=100`, `arcade_ending_rank.txt` (PSP: `port/psp/scripts/arcade_psp.txt`) plays a whole arcade run to the
credits (~27,000 frames). Its `shot` BMPs currently capture the debug text,
not the game. `PRESS=frame:len:bits,...` in `env.txt` (testing) holds PS2 bits (hex,
`pad.h`) on player 1, e.g. `PRESS=600:6:8` presses Start at frame 600.

Saves (P57-P59, `psp_saveui.c`, `psp_save.c`): one PSP system savedata
entry, `ms0:/PSP/SAVEDATA/MVC2PSP000000/DATA.BIN`, holding the game's
0x754-byte save block. Boot (front end 1/8) loads it without a dialog and
skips the PS2 card screen; main menu SAVE / LOAD open the system Save / Load
list dialogs (drawn from `r_gu_present`, icon from `tools/make_icon0.py`);
the game's autosaves (`func_0038B2E0`) store it without a dialog. The
autosave screens still check the PS2 card through `mc.c`. Scripted arcade
runs use `port/psp/scripts/arcade_psp.txt`, which presses through the
arcade-end autosave prompt. `VERBOSE=1` logs libmc requests.

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
