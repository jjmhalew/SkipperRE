#!/bin/sh
# build.sh - bouwt SkipperRE.
#   Windows (Git Bash): ./build.sh -> out/skipper.exe, met Zig (pip install ziglang). RELEASE=1 voor de echte build.
#   Linux:              ./build.sh -> out/skipper, met cc en SDL2 (Debian / Ubuntu: sudo apt install build-essential libsdl2-dev)
#   Android:            zie android/ (Gradle).
# OUT=... kiest het doelbestand, OPT=... de optimalisatie, DEFS=... extra opties.
cd "$(dirname "$0")"
if [ "$RELEASE" = 1 ]; then OPT=${OPT:--O2 -fno-sanitize=undefined}; GUI=1; fi
OPT=${OPT:--O1}
SRC="src/main.c src/host_win.c src/host_sdl.c src/plat_win.c src/plat_posix.c src/ini.c src/text_gdi.c src/text_ttf.c
     src/stb_impl.c src/dfile.c src/lingo.c src/builtins.c src/player.c src/stage.c src/xobj.c src/sound.c src/video.c
     src/trans.c src/disc.c src/pack.c src/dbgheap.c src/pad.c src/pad_sdl.c src/padinput.c src/texpack.c"
mkdir -p out
case "$(uname -s)" in
Linux*)
  OUT=${OUT:-out/skipper}
  CC=${CC:-cc}
  # shellcheck disable=SC2086
  $CC -std=gnu99 $OPT -g -Wall -Wno-unused-function -Wno-format-truncation -D_FILE_OFFSET_BITS=64 -o "$OUT" $SRC $DEFS \
    $(sdl2-config --cflags) $(sdl2-config --libs) -lm -lpthread
  ;;
*)
  OUT=${OUT:-out/skipper.exe}
  # versie (res/skipperre.rc) en icoon: dat van de cd als de spelbestanden in extract/ staan, anders res/skipperre.ico
  ICON=${ICON:-extract/Magnus.ico}
  [ -f "$ICON" ] || ICON=res/skipperre.ico
  cp "$ICON" out/skipper.ico
  { cat res/skipperre.rc; echo '1 ICON "skipper.ico"'; } > out/skipper.rc
  RC=out/skipper.rc
  [ "$GUI" = 1 ] && DEFS="$DEFS -Wl,--subsystem,windows"   # geen consolevenster naast het spel
  # shellcheck disable=SC2086
  python -m ziglang cc -std=c99 $OPT -g -fno-omit-frame-pointer -Wall -Wno-unused-function -o "$OUT" $SRC $RC $DEFS \
    -lgdi32 -luser32 -lwinmm -ldbghelp -lcomdlg32 -lsetupapi -lhid
  ;;
esac
