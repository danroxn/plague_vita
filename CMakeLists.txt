cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
endif()
project(plague_vita LANGUAGES C CXX)

set(PORT_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/work/assets" CACHE PATH
    "Artwork, trophies and APK identity prepared once with tools/prepare_assets.py")

add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=/src"
                    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=/build")
include(cmake/VitaGL.cmake)

file(GLOB_RECURSE PORT_SOURCES CONFIGURE_DEPENDS "src/*.c")
include(cmake/BuildInfo.cmake)

add_executable(plague_vita ${PORT_SOURCES} third_party/bionic/ctype.c
               third_party/minizip/unzip.c third_party/minizip/ioapi.c)
set_target_properties(plague_vita PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES LINKER_LANGUAGE C)
target_include_directories(plague_vita PRIVATE
  src third_party third_party/so_util third_party/vitaGL/source third_party/jni
  "${PORT_GENERATED_DIR}" "${PORT_ASSET_DIR}")
target_compile_definitions(plague_vita PRIVATE _GNU_SOURCE)
target_compile_options(plague_vita PRIVATE -O2 -Wall -Wextra -fno-builtin)

# APK support needs stored/deflate reads, without archive encryption backends.
set_source_files_properties(third_party/minizip/unzip.c third_party/minizip/ioapi.c
                           PROPERTIES COMPILE_DEFINITIONS "NOUNCRYPT;USE_FILE32API")
set_source_files_properties(src/app/loading_indicator.c
                           PROPERTIES COMPILE_OPTIONS "-fbuiltin;-fno-math-errno;-mtune=cortex-a9")
set_source_files_properties(src/runtime/apk.c
                           PROPERTIES COMPILE_OPTIONS "-fbuiltin;-mtune=cortex-a9")
set_source_files_properties(src/runtime/loader_integration.c PROPERTIES COMPILE_OPTIONS
                           "-Wno-unused-value;-Wno-unused-parameter;-Wno-sign-compare")

# Leave room for VitaSDK's import metadata between the ELF segments.
target_link_options(plague_vita PRIVATE "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/tools/vita_sce.ld")
set_property(TARGET plague_vita APPEND PROPERTY LINK_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/tools/vita_sce.ld")

target_link_libraries(plague_vita PRIVATE
  vitaGL
  SDL2_mixer SDL2 mpg123 vorbisfile vorbis ogg FLAC opusfile opus modplug xmp sndfile mp3lame
  stdc++ crypto png16 webpdecoder z pthread
  SceLibKernel_stub SceAppMgr_stub SceAppUtil_stub mathneon c SceCommonDialog_stub SceNpTrophy_stub m
  SceGxm_stub SceDisplay_stub SceSysmodule_stub zip z vitashark SceShaccCgExt taihen_stub SceShaccCg_stub
  SceKernelDmacMgr_stub SceCtrl_stub SceTouch_stub SceAudio_stub SceAudioIn_stub SceMotion_stub
  SceHid_stub ScePower_stub SceRtc_stub SceNet_stub SceNetCtl_stub SceIme_stub kubridge_stub)

include(cmake/Package.cmake)
