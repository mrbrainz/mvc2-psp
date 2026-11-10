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

## Game library

The game TUs need clang (clang-only flags; the EE float rewrite works on LLVM
IR), so they build with Homebrew LLVM's mipsel backend (`brew install llvm`):

    cmake -S port -B build/port-psp -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE=$PWD/port/cmake/psp-clang.cmake -DMVC2_APP=OFF
    ninja -C build/port-psp mvc2_game

All 18,440 objects compile (MIPS II, single-float FPU, `-G0`). clang emits the
o32 ABI; psp-gcc and the PSP SDK use EABI32, so calls across that boundary
must be reconciled before linking.

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
- `libc_o32.c` defines o32 `snprintf`/`vsnprintf`/`fprintf`/`vfprintf`/`sscanf`
  for the whole link, so EABI code (psp-gcc files, PSPSDK libraries) must not
  call those: use newlib's `sniprintf` or `pspDebugScreenPrintf` there.

    psp-cmake -S port/psp/abi_test -B build/psp-abi && cmake --build build/psp-abi

## Headless EBOOT (mvc2_host)

    ninja -C build/port-psp mvc2_eboot     # after the cmake line above

`port/psp/host/psp_entry.c` (psp-gcc) is the module entry; it calls the o32
runner `psp_main.c`: boundary self-test, boot, 600 frames, a logic hash every
60 frames, and a state dump for `port/tools/oracle/compare.py`.
`libc_o32.c` replaces the newlib functions that cannot cross the boundary
(printf/scanf family, `fmaf`, `fseeko` via `--wrap`). Other link details:
`-mno-gpopt` (the source's `section(".sdata")` externs), psp-gcc links with
`-fno-use-linker-plugin -G0`, and gen_link_glue reads pspdev's libs when
`MVC2_PSP=1` (set by CMake).

Run in PPSSPP (memory stick is `~/.config/ppsspp`):

    G=~/.config/ppsspp/PSP/GAME/mvc2; mkdir -p $G
    cp build/port-psp/EBOOT.PBP $G/
    ln -sf "$PWD/<your ISO>" $G/mvc2.iso     # never copied into git
    /Applications/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL $G/EBOOT.PBP
    # output: $G/mvc2_psp.log, $G/state.bin

`ra_state_hash` covers host pointers, so it only compares runs of one binary.

## PSP vs wasm state check

`compare.py` is for port vs oracle; between two ports its block hashes flag
pointers neither side can translate (function pointers, host malloc blocks),
and wasm's `fix()` mistakes integers such as `0x01000000` for heap addresses.
Use `compare_ports.py`, which reads the whole BSS fixed and raw:

    X=0x004C2580:0x00634380
    printf 'FRAMES=600\nMVC2_DUMP_EXTRA=%s\nMVC2_DUMP_EXTRA_RAW=1\n' $X > $G/env.txt   # PSP env (psp_main)
    MVC2_DUMP_EXTRA=$X MVC2_DUMP_EXTRA_RAW=1 MVC2_STATE_DUMP=ref.bin node build/port-em/mvc2_host.js --iso <ISO> --frames 600
    python3 port/tools/oracle/compare_ports.py $G/state.bin ref.bin --ignore port/tools/oracle/psp_vs_wasm.ignore

About 3 MB per frame per side. Result (title attract, no input): 600 frames,
0 diverging words; 78 host-pointer words and the boot clock differ.

## Reference build (wasm, under node)

`brew install emscripten`; the port's tools expect an emsdk layout, so point
`EMSDK` at a folder of symlinks:

    E=~/Sites/emsdk-brew; L=/opt/homebrew/opt/emscripten/libexec
    mkdir -p $E/upstream; ln -sfn $L/llvm/bin $E/upstream/bin; ln -sfn $L $E/upstream/emscripten
    EMSDK=$E emcmake cmake -S port -B build/port-em -G Ninja -DMVC2_APP=OFF
    EMSDK=$E ninja -C build/port-em mvc2_host
    MVC2_STATE_DUMP=ref.bin node build/port-em/mvc2_host.js --iso <ISO> --frames 600 [--hash-every 60]
    python3 port/tools/oracle/compare.py $G/state.bin ref.bin

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
cannot hold the EE heap. `[mem]` lines in `mvc2_psp.log` report malloc use.

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
