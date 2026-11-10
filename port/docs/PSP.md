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
Cross-platform checks use the state dump.

## Reference build (wasm, under node)

`brew install emscripten`; the port's tools expect an emsdk layout, so point
`EMSDK` at a folder of symlinks:

    E=~/Sites/emsdk-brew; L=/opt/homebrew/opt/emscripten/libexec
    mkdir -p $E/upstream; ln -sfn $L/llvm/bin $E/upstream/bin; ln -sfn $L $E/upstream/emscripten
    EMSDK=$E emcmake cmake -S port -B build/port-em -G Ninja -DMVC2_APP=OFF
    EMSDK=$E ninja -C build/port-em mvc2_host
    MVC2_STATE_DUMP=ref.bin node build/port-em/mvc2_host.js --iso <ISO> --frames 600 [--hash-every 60]
    python3 port/tools/oracle/compare.py $G/state.bin ref.bin
