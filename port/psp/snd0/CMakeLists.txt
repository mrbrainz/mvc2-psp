# mvc2-snd0: makes the EBOOT's XMB music (SND0.AT3) from the player's own
# disc, for the EBOOT builder and psp_pack.py. The ATRAC3 encoder is atracdenc
# (LGPL-2.1, https://github.com/dcherednik/atracdenc), fetched at a pinned
# commit and built unchanged, except that its PCM input is pcm_wav.cpp here
# (no libsndfile or Media Foundation). One static binary per platform:
#   cmake -S port/psp/snd0 -B build/snd0 -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/snd0 --config Release
# Output: build/snd0/bin/ (mvc2-snd0 and atracdenc's licence).
cmake_minimum_required(VERSION 3.18)
cmake_policy(SET CMP0091 NEW)   # MSVC runtime selection
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
project(mvc2_snd0 C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

include(FetchContent)
FetchContent_Declare(atracdenc
  GIT_REPOSITORY https://github.com/dcherednik/atracdenc.git
  GIT_TAG cb170709f836cb43dc0312455188c791159d81dc
  SOURCE_SUBDIR none)   # its own CMakeLists is not used
FetchContent_MakeAvailable(atracdenc)
set(A "${atracdenc_SOURCE_DIR}/src")

set(SRC
  lib/fft/kissfft_impl/kiss_fft.c lib/fft/kissfft_impl/tools/kiss_fftr.c
  lib/libgha/src/gha.c lib/libgha/src/sle.c
  lib/liboma/src/liboma.c lib/bitstream/bitstream.cpp
  main.cpp help.cpp wav.cpp aea.cpp env.cpp transient_detector.cpp
  transient_spectral_upsampler.cpp atrac1denc.cpp atrac/at1/atrac1.cpp
  atrac/at1/atrac1_bitalloc.cpp atrac/at1/atrac1_dequantiser.cpp
  atrac/atrac_scale.cpp atrac/atrac_enc_cache.cpp atrac/atrac_psy_common.cpp
  oma.cpp rm.cpp at3.cpp raw.cpp atrac3denc.cpp atrac/at3/atrac3.cpp
  atrac/at3/atrac3_bitstream.cpp atrac/atrac3plus_pqf/atrac3plus_pqf.c
  atrac/at3p/ff/atrac3plusdsp.c atrac/at3p/at3p.cpp atrac/at3p/at3p_bitstream.cpp
  atrac/at3p/at3p_gha.cpp atrac/at3p/at3p_mdct.cpp atrac/at3p/at3p_tables.cpp
  lib/mdct/mdct.cpp lib/bs_encode/encode.cpp qmf/qmf.cpp)
list(TRANSFORM SRC PREPEND "${A}/")
# its main() is not the program's: snd0.cpp calls its main_()
set_source_files_properties("${A}/main.cpp" PROPERTIES COMPILE_DEFINITIONS main=atracdenc_main)

add_executable(mvc2-snd0 snd0.cpp pcm_wav.cpp ../../audio/adx.c ${SRC})
target_include_directories(mvc2-snd0 PRIVATE "${A}" "${A}/lib"
  "${A}/lib/liboma/include" "${A}/lib/fft/kissfft_impl" "${A}/lib/libgha"
  ../../audio)   # after atracdenc: both have a wav.h
if(WIN32)
  target_compile_definitions(mvc2-snd0 PRIVATE PLATFORM_WINDOWS _CRT_SECURE_NO_WARNINGS
    _USE_MATH_DEFINES LIBGHA_PLATFORM_WINDOWS)
  target_include_directories(mvc2-snd0 PRIVATE "${A}/platform/win/getopt")
  target_link_libraries(mvc2-snd0 shell32)
else()
  target_link_libraries(mvc2-snd0 m)
endif()
if(NOT MSVC)
  target_compile_options(mvc2-snd0 PRIVATE -ffp-contract=off)
  if(NOT APPLE)
    target_link_options(mvc2-snd0 PRIVATE -static)
  endif()
endif()
set_target_properties(mvc2-snd0 PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/bin>")   # same with Visual Studio
add_custom_command(TARGET mvc2-snd0 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${atracdenc_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/bin/atracdenc-LICENSE.txt")
