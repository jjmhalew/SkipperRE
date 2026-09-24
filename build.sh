#!/bin/sh
# Bouwen (debug: UBSan + PDB; zet OPT=-O2 voor een release-build)
OPT=${OPT:--O1}
python -m ziglang cc -std=c99 $OPT -g -fno-omit-frame-pointer -Wall -Wno-unused-function -o out/skipper.exe \
  src/main.c src/dfile.c src/lingo.c src/builtins.c src/player.c src/stage.c src/xobj.c src/sound.c src/dbgheap.c $DEFS \
  -lgdi32 -luser32 -lwinmm -ldbghelp
