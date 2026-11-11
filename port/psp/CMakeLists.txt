# PSP build of the port, kept apart from upstream: port/CMakeLists.txt is
# included unchanged and only its game targets (mvc2_game, mvc2_game_data)
# are used; the glue, host core and EBOOT are built here.
#   EMSDK=~/Sites/emsdk-brew cmake -S port/psp -B build/port-psp -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=$PWD/port/cmake/psp-clang.cmake
#   EMSDK=~/Sites/emsdk-brew ninja -C build/port-psp mvc2_eboot
# See port/docs/PSP.md.
cmake_minimum_required(VERSION 3.20)
project(mvc2_psp C)
if(NOT MVC2_PSP)
  message(FATAL_ERROR "use -DCMAKE_TOOLCHAIN_FILE=port/cmake/psp-clang.cmake")
endif()
get_filename_component(MVC2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(MVC2_APP OFF CACHE BOOL "the SDL2/GL app is not built for the PSP" FORCE)
add_subdirectory("${MVC2_ROOT}/port" upstream EXCLUDE_FROM_ALL)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# long object lists (psp-ar takes @file)
set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON)

set(PSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(PSP_INC "${PSPDEV}/psp/include" "${PSPDEV}/psp/sdk/include")

# ---- glue: gen_link_glue.py through a wrapper that knows the PSP libs ------
set(GLUE "${CMAKE_BINARY_DIR}/psp_glue")
set(GLUE_SRCS "${GLUE}/mvc2_glue_data.c" "${GLUE}/mvc2_glue_stubs.c" "${GLUE}/mvc2_glue_table.c")
file(GLOB STUB_SRCS "${MVC2_ROOT}/port/stubs/*.c")
list(FILTER STUB_SRCS EXCLUDE REGEX "host_main\\.c$")   # POSIX runner; PSP: psp_main.c
get_filename_component(_llvm_bin "${CMAKE_C_COMPILER}" DIRECTORY)
add_custom_command(
  OUTPUT ${GLUE_SRCS} "${GLUE}/link_extra.rsp" "${GLUE}/stubs.csv" "${GLUE}/link_glue.csv"
         "${GLUE}/mvc2_host_protos.h"
  COMMAND ${CMAKE_COMMAND} -E env "PSPDEV=${PSPDEV}" "PATH=${_llvm_bin}:$ENV{PATH}"
          ${Python3_EXECUTABLE} "${PSP_DIR}/tools/psp_glue.py" glue
          --out "${GLUE}" --target i386 --sigs "${CMAKE_BINARY_DIR}/host_sigs.json"
          "$<TARGET_FILE:mvc2_game>"
  DEPENDS mvc2_game "${PSP_DIR}/tools/psp_glue.py" "${MVC2_ROOT}/port/tools/gen_link_glue.py"
          "${MVC2_ROOT}/port/tools/link_catalog.py" ${STUB_SRCS} "${MVC2_ROOT}/port/host/host_impl.csv"
  WORKING_DIRECTORY "${MVC2_ROOT}"
  COMMENT "psp_glue.py: BSS/data/stub glue for the PSP link")

# ---- host core (as upstream's mvc2_host_core, with the PSP libc headers) ---
file(GLOB HOST_CORE_SRCS CONFIGURE_DEPENDS "${MVC2_ROOT}/port/host/*.c")
set(PLATFORM_CORE_SRCS "${MVC2_ROOT}/port/platform/fs.c" "${MVC2_ROOT}/port/platform/pad.c"
  "${MVC2_ROOT}/port/audio/tsd_client.c")
add_library(psp_host_core OBJECT ${HOST_CORE_SRCS} ${PLATFORM_CORE_SRCS})
target_include_directories(psp_host_core PRIVATE "${MVC2_ROOT}/port/stubs" "${MVC2_ROOT}/port/host"
  "${MVC2_ROOT}/port/platform" "${GLUE}")
target_include_directories(psp_host_core SYSTEM PRIVATE ${PSP_INC})
target_compile_options(psp_host_core PRIVATE -fno-common -Wall -Wno-unused-function)
set_source_files_properties(${HOST_CORE_SRCS} PROPERTIES OBJECT_DEPENDS "${GLUE}/mvc2_host_protos.h")

# ---- psp-gcc (EABI32) objects: module entry, sceGu shims; the rest is o32 --
set(PSP_EABI_OBJS "")
foreach(f psp_entry psp_gu_shim psp_audio_hw psp_disc_io)
  set(_o "${CMAKE_BINARY_DIR}/${f}.o")
  add_custom_command(OUTPUT ${_o}
    COMMAND ${PSPDEV}/bin/psp-gcc -O2 -G0 -Wall -I${PSPDEV}/psp/sdk/include -D_PSP_FW_VERSION=600
            -c "${PSP_DIR}/host/${f}.c" -o ${_o}
    DEPENDS "${PSP_DIR}/host/${f}.c" "${PSP_DIR}/host/psp_entry.h" "${PSP_DIR}/host/psp_gu_shim.h" "${PSP_DIR}/host/psp_audio_hw.h" "${PSP_DIR}/host/psp_disc_io.h")
  list(APPEND PSP_EABI_OBJS ${_o})
endforeach()

# The ~16k EE data units go in through an archive (their paths overflow the
# command line collect2 builds), whole: every unit is kept, as upstream links
# them all; --sort-section=name puts them in EE order regardless.
add_library(psp_game_data STATIC $<TARGET_OBJECTS:mvc2_game_data>)
set_target_properties(psp_game_data PROPERTIES LINKER_LANGUAGE C)

# Audio (P31): port/audio's mixer unchanged, audio_psp.c in place of the SDL
# audio.c, out through sceAudio (psp_audio_hw.c).
set(PSP_AUDIO_SRCS "${MVC2_ROOT}/port/audio/audio_psp.c" "${MVC2_ROOT}/port/audio/adx.c"
  "${MVC2_ROOT}/port/audio/dcse.c" "${MVC2_ROOT}/port/audio/hdbank.c" "${MVC2_ROOT}/port/audio/resample.c"
  "${MVC2_ROOT}/port/audio/synth.c" "${MVC2_ROOT}/port/audio/tsd.c" "${MVC2_ROOT}/port/audio/vag.c"
  "${MVC2_ROOT}/port/audio/mix_psp.c" "${PSP_DIR}/host/psp_audio.c")

# Renderer: r_gu.c (sceGu) behind upstream's frontend binding fe_render.c,
# which is compiled unchanged; decode.c (TIM2) is GL-free.
set(PSP_RENDER_SRCS "${MVC2_ROOT}/port/render/r_gu.c" "${MVC2_ROOT}/port/render/decode.c"
  "${MVC2_ROOT}/port/render/r_gu_char.c" "${MVC2_ROOT}/port/render/r_gu_mesh.c" "${MVC2_ROOT}/port/host/fe/fe_render.c" "${MVC2_ROOT}/port/host/fe/fe_char.c"
  "${MVC2_ROOT}/port/host/fe/fe_mesh.c")
set_source_files_properties("${MVC2_ROOT}/port/host/fe/fe_render.c" "${MVC2_ROOT}/port/host/fe/fe_char.c" "${MVC2_ROOT}/port/host/fe/fe_mesh.c" PROPERTIES OBJECT_DEPENDS "${GLUE}/mvc2_host_protos.h")

add_executable(mvc2_psp ${STUB_SRCS} ${GLUE_SRCS} ${PSP_EABI_OBJS} ${PSP_RENDER_SRCS} ${PSP_AUDIO_SRCS}
  "${PSP_DIR}/host/psp_main.c" "${PSP_DIR}/host/libc_o32.c" "${PSP_DIR}/host/psp_fpu.c"
  "${PSP_DIR}/host/psp_prof.c" "${PSP_DIR}/host/psp_fe_sync.c" "${PSP_DIR}/host/psp_disc.c" "${PSP_DIR}/host/psp_script.c" "${MVC2_ROOT}/port/platform/platform_psp.c"
  $<TARGET_OBJECTS:psp_host_core>)
target_include_directories(mvc2_psp PRIVATE "${MVC2_ROOT}/port/stubs" "${PSP_DIR}/host"
  "${MVC2_ROOT}/port/host" "${MVC2_ROOT}/port/platform" "${GLUE}")
target_include_directories(mvc2_psp SYSTEM PRIVATE ${PSP_INC})
target_compile_options(mvc2_psp PRIVATE -fno-common)
target_link_libraries(mvc2_psp PRIVATE "-Wl,--whole-archive" psp_game_data "-Wl,--no-whole-archive"
  mvc2_game pspgu pspdebug pspdisplay pspge pspctrl psprtc psppower pspaudio m)
# --sort-section=name: the .data.ee_<address> sections in EE order (R18).
# --wrap: o32 replacements and PSP-side hooks (port/psp/host).
target_link_options(mvc2_psp PRIVATE -no-pie "@${GLUE}/link_extra.rsp" -Wl,--sort-section=name
  -Wl,--wrap=fseeko -Wl,--wrap=ps2_float_set_nearest
  -Wl,--wrap=mvc2_frame -Wl,--wrap=func_0012F460 -Wl,--wrap=func_001E42A0
  -Wl,--wrap=func_003D6FF0 -Wl,--wrap=func_00170D30 -Wl,--wrap=func_003D7400
  -Wl,--wrap=func_003D7640 -Wl,--wrap=func_0012DD90 -Wl,--wrap=func_00170D70
  -Wl,--wrap=func_003ECD00 -Wl,--wrap=func_003F0500 -Wl,--wrap=fl_clay_draw
  -Wl,--wrap=func_00412260 -Wl,--wrap=func_0041BB30 -Wl,--wrap=fe_sync_state
  -Wl,--wrap=func_003F0500__game -Wl,--wrap=func_003EA660
  -Wl,--wrap=fe_sync_handle -Wl,--wrap=func_00170170
  -Wl,--wrap=synth_render -Wl,--wrap=rs_stream_run -Wl,--wrap=hd_bank_load)

# ---- EBOOT.PBP: fixup imports, strip, SFO (MEMSIZE=1: Slim memory), pack --
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/EBOOT.PBP"
  COMMAND ${PSPDEV}/bin/psp-fixup-imports -o mvc2_psp.elf mvc2_psp
  COMMAND ${PSPDEV}/bin/psp-strip mvc2_psp.elf -o mvc2_psp.strip.elf
  COMMAND ${PSPDEV}/bin/mksfoex -d MEMSIZE=1 "MvC2 PSP" PARAM.SFO
  COMMAND ${PSPDEV}/bin/pack-pbp EBOOT.PBP PARAM.SFO NULL NULL NULL NULL NULL mvc2_psp.strip.elf NULL
  DEPENDS mvc2_psp WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_target(mvc2_eboot ALL DEPENDS "${CMAKE_BINARY_DIR}/EBOOT.PBP")
