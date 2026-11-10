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
