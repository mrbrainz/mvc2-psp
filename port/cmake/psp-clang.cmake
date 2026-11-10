# PSP (Allegrex, 32-bit little-endian MIPS) build of the game TUs.
# The game code needs clang (MVC2_HOST_OPTIONS, the EE float rewrite in
# ps2fp-cc.pl works on LLVM IR), so it is compiled with LLVM's mipsel backend;
# the PSP SDK side (platform shell, link, EBOOT) uses pspdev's psp-gcc.
#   cmake -S port -B build/port-psp -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=$PWD/port/cmake/psp-clang.cmake -DMVC2_APP=OFF
#   ninja -C build/port-psp mvc2_game
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
# libgcc soft-float calls), no small-data section.
set(CMAKE_C_FLAGS_INIT "-mips2 -mfp32 -msingle-float -mhard-float -G0 -mno-abicalls -fno-pic -DPSP -D__PSP__")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(MVC2_PSP ON)
