cmake_minimum_required(VERSION 3.10)
project(mvc2_abi_test C)
# o32 object from clang with the same flags as port/cmake/psp-clang.cmake
set(O32_OBJ ${CMAKE_CURRENT_BINARY_DIR}/o32_side.o)
add_custom_command(OUTPUT ${O32_OBJ}
  COMMAND /opt/homebrew/opt/llvm/bin/clang --target=mipsel-unknown-elf -mips2 -mfp32
          -msingle-float -mhard-float -G0 -mno-abicalls -fno-pic -O2
          -c ${CMAKE_CURRENT_SOURCE_DIR}/o32_side.c -o ${O32_OBJ}
  DEPENDS o32_side.c abi_shim.h)
add_executable(mvc2_abi_test eabi_side.c ${O32_OBJ})
target_link_libraries(mvc2_abi_test pspdebug pspdisplay pspge)
# clang objects are o32, the SDK EABI32; the boundary rules are in abi_shim.h
target_link_options(mvc2_abi_test PRIVATE -Wl,--no-warn-mismatch)
create_pbp_file(TARGET mvc2_abi_test TITLE "MvC2 PSP ABI test")
