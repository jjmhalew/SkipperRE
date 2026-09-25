#!/bin/sh
# Bouwen: standaard debug (UBSan + PDB); RELEASE=1 voor een geoptimaliseerde build zonder UBSan.
#   ./build.sh                       out/skipper.exe (debug)
#   RELEASE=1 ./build.sh             out/skipper.exe (release)
#   OPT=-O0 DEFS=-DDBGHEAP ./build.sh  met debug-heap
if [ "$RELEASE" = 1 ]; then OPT=${OPT:--O2 -fno-sanitize=undefined}; fi
OPT=${OPT:--O1}
OUT=${OUT:-out/skipper.exe}
python -m ziglang cc -std=c99 $OPT -g -fno-omit-frame-pointer -Wall -Wno-unused-function -o $OUT \
  src/main.c src/dfile.c src/lingo.c src/builtins.c src/player.c src/stage.c src/xobj.c src/sound.c src/video.c src/trans.c src/disc.c src/dbgheap.c $DEFS \
  -lgdi32 -luser32 -lwinmm -ldbghelp -lcomdlg32
