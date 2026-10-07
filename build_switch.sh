#!/bin/sh
# build_switch.sh - builds SkipperRE for a Nintendo Switch with homebrew (custom firmware such as Atmosphere):
#   ./build_switch.sh -> skipperre.nro; copy it to /switch/skipperre/ on the SD card and start it from the Homebrew Menu.
# Needs devkitPro's devkitA64 with switch-sdl2 (devkitpro.org). Without $DEVKITPRO it builds in the official
# devkitpro/devkita64 Docker image instead, which has all of that. The game files come from your own CD; see README.md.
set -e
cd "$(dirname "$0")"
if [ -z "$DEVKITPRO" ]; then
    echo "DEVKITPRO is not set: building in the devkitpro/devkita64 Docker image ..."
    exec docker run --rm -v "$PWD":/src -w /src devkitpro/devkita64 sh ./build_switch.sh "$@"
fi
OUT=${1:-skipperre}
VERSION=$(sed -n 's/^FILEVERSION *\([0-9]*\),\([0-9]*\),\([0-9]*\).*/\1.\2.\3/p' res/skipperre.rc)
PORT=$DEVKITPRO/portlibs/switch
PATH=$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH
# the same engine files as the Android build (android/app/CMakeLists.txt), with switch.c in place of android.c
SRC="src/main.c src/host_sdl.c src/plat_posix.c src/ini.c src/text_ttf.c src/stb_impl.c src/dfile.c src/lingo.c
     src/builtins.c src/player.c src/stage.c src/xobj.c src/sound.c src/video.c src/trans.c src/disc.c src/pack.c
     src/dbgheap.c src/pad_sdl.c src/padinput.c src/texpack.c src/songs.c src/switch.c src/touch.c"
ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
# char is unsigned on ARM; the engine is written for x86, where it is signed
CFLAGS="-std=gnu99 -O2 -ffunction-sections -fsigned-char -Wall -Wno-unused-function -Wno-format-truncation $ARCH -D__SWITCH__
        -D_FILE_OFFSET_BITS=64 -Isrc -I$PORT/include -I$PORT/include/SDL2 -I$DEVKITPRO/libnx/include"
LIBS="-L$PORT/lib -L$DEVKITPRO/libnx/lib -lSDL2 -lGLESv2 -lEGL -lglapi -ldrm_nouveau -lstdc++ -lnx -lpthread -lm"
mkdir -p out/switch
echo "Building $OUT.nro (SkipperRE $VERSION) ..."
# shellcheck disable=SC2086
aarch64-none-elf-gcc $CFLAGS -specs="$DEVKITPRO/libnx/switch.specs" -Wl,-Map,out/switch/$OUT.map -o out/switch/$OUT.elf $SRC $LIBS
nacptool --create "SkipperRE" "jjmhalew" "$VERSION" out/switch/$OUT.nacp
elf2nro out/switch/$OUT.elf "$OUT.nro" --icon=res/switch_icon.jpg --nacp=out/switch/$OUT.nacp
echo "Done: $OUT.nro"
