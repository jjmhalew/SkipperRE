# The native part of the Android build: SDL2 (fetched, the release whose Java glue is in src/main/java/org/libsdl) and the
# engine of ../../src as libmain.so (the same files as build.sh; host_sdl.c draws through SDL's renderer on OpenGL ES).
cmake_minimum_required(VERSION 3.18)
project(SkipperRE C)

include(FetchContent)
FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
    URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

set(S ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_library(main SHARED
    ${S}/main.c ${S}/host_sdl.c ${S}/plat_posix.c ${S}/ini.c ${S}/text_ttf.c ${S}/stb_impl.c ${S}/dfile.c ${S}/lingo.c
    ${S}/builtins.c ${S}/player.c ${S}/stage.c ${S}/xobj.c ${S}/sound.c ${S}/video.c ${S}/trans.c ${S}/disc.c ${S}/pack.c
    ${S}/dbgheap.c ${S}/pad_sdl.c ${S}/padinput.c ${S}/texpack.c ${S}/android.c)
target_include_directories(main PRIVATE ${S})
# char is unsigned on ARM; the engine is written for x86, where it is signed
target_compile_options(main PRIVATE -std=gnu99 -O2 -fsigned-char -Wno-format-truncation -D_FILE_OFFSET_BITS=64)
target_link_libraries(main PRIVATE SDL2::SDL2 log android m)
