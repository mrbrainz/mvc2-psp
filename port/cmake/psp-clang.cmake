# PSP (Allegrex, 32-bit little-endian MIPS) build of the game TUs.
# The game code needs clang (MVC2_HOST_OPTIONS, the EE float rewrite in
# ps2fp-cc.pl works on LLVM IR), so it is compiled with LLVM's mipsel backend;
# the PSP SDK side (platform shell, link, EBOOT) uses pspdev's psp-gcc.
#   EMSDK=~/Sites/emsdk-brew cmake -S port/psp -B build/port-psp -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=$PWD/port/cmake/psp-clang.cmake
#   EMSDK=~/Sites/emsdk-brew ninja -C build/port-psp mvc2_eboot
# Needs: Homebrew llvm (clang with the Mips target), PSPDEV (port/docs/PSP.md).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR mipsel)
if(NOT DEFINED ENV{PSPDEV})
  set(ENV{PSPDEV} "$ENV{HOME}/Sites/pspdev")
endif()
set(PSPDEV "$ENV{PSPDEV}")
find_program(MVC2_PSP_CLANG clang PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin NO_DEFAULT_PATH)
set(CMAKE_C_COMPILER "${MVC2_PSP_CLANG}")
set(CMAKE_C_COMPILER_TARGET mipsel-unknown-elf)
set(CMAKE_AR "${PSPDEV}/bin/psp-ar")
set(CMAKE_RANLIB "${PSPDEV}/bin/psp-ranlib")
# Allegrex: MIPS II integer core, single-precision FPU (doubles go through
# libgcc soft-float calls), no small-data section; -mno-gpopt because the
# source marks EE gp-relative externs section(".sdata") (776 sites), which on
# the PSP are aliases into the EE image, out of gp range.
set(CMAKE_C_FLAGS_INIT "-O2 -mips2 -mfp32 -msingle-float -mhard-float -G0 -mno-gpopt -mno-abicalls -fno-pic -DPSP -D__PSP__")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(MVC2_PSP ON)
# EE float rounding on the FPU itself (FCSR, port/host/ps2_float.c), not the
# ps2_fp_rewrite helpers, which run in soft-float double here (6-38 ms/frame)
set(MVC2_PS2_FLOAT OFF CACHE BOOL "EE single-precision rounding for the game code")
# Link with psp-gcc (PSPSDK crt0, newlib, libgcc); objects mix o32 and EABI32,
# see port/docs/PSP.md "o32 / EABI32 boundary".
set(CMAKE_C_LINK_EXECUTABLE
  "${PSPDEV}/bin/psp-gcc -L${PSPDEV}/psp/sdk/lib -Wl,--no-warn-mismatch -fno-use-linker-plugin -G0 <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
