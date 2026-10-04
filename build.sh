#!/bin/sh
if [ "$RELEASE" = 1 ]; then OPT=${OPT:--O2 -fno-sanitize=undefined}; fi
OPT=${OPT:--O1}
OUT=${OUT:-out/skipper.exe}
# icoon van de cd in de exe (als de spelbestanden in extract/ staan)
ICON=${ICON:-extract/Magnus.ico}
RC=
if [ -f "$ICON" ]; then
  mkdir -p out
  cp "$ICON" out/skipper.ico
  echo '1 ICON "skipper.ico"' > out/skipper.rc
  RC=out/skipper.rc
fi
python -m ziglang cc -std=c99 $OPT -g -fno-omit-frame-pointer -Wall -Wno-unused-function -o $OUT \
  src/main.c src/host_win.c src/plat_win.c src/ini.c src/text_gdi.c src/dfile.c src/lingo.c src/builtins.c src/player.c src/stage.c src/xobj.c src/sound.c src/video.c src/trans.c src/disc.c src/pack.c src/dbgheap.c $RC $DEFS \
  -lgdi32 -luser32 -lwinmm -ldbghelp -lcomdlg32
