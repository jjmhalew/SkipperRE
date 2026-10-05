# Releases of the game and what the port plays

The game is called *Magnus og Myggen* in Denmark, *Magnus & Myggen* in Norway, *Magnus och Myggan* in Sweden,
*Manu ja Matti* in Finland and *Skipper & Skeeto in Pretpark* in the Netherlands. Examined with images from archive.org
(October 2026), unpacked with [tools/discx.py](../tools/discx.py) (ISO, BIN, CloneCD IMG, Joliet) and `unshield`
(InstallShield), compared with [tools/reldiff.py](../tools/reldiff.py) and the Lingo decompiler.

| release | archive.org | image (SHA-1) | Director | port |
|---|---|---|---|---|
| Dutch, Transposia 1997 (volume `SKIPPER_1`) | `skipper-1` | `SKIPPER_1.BIN` `4d0ca5bc…` | 5 (1217) | yes (the reference) |
| Dutch, 2003 re-pressing (`SKIPPER_1`, Joliet) | `skipper1` | `SKIPPER_1.bin` `58600eaf…` | 5 | yes |
| Nordic, Ivanoff, "Magnus 1.5" (`MAGNUS15S`), four languages | `magnus-mygg-1` | `MagnusMygg1.img` `40fb3afe…` (CloneCD) | 5 (1217) | yes |
| Danish, first release 1996 (`MAGNUS11`) | `magnus-myggen-leg-og-laer` | `Magnus-Myggen-Leg-og-Laer.iso` `27f69c23…` | 4 (1117) | yes (since 0.9.7) |
| Danish, *Superstarter* 2006 (`M122DK`) | `leg-og-laer-superstarter-version` | `Leg og Lær - Superstarter Version.iso` `e3e639b2…` | 8.5 (1850) | no |
| Swedish, *Lek och Lär*, re-release (`M_M_1`) | `m-m-1` | `M&M 1.ISO` `b89c078d…` | 10 (1860) | no |

Also found but not examined: German (*Max & Mario*), Finnish and Norwegian releases of the later parts; of this first
part no German or English CD.

## 1. The Dutch 2003 re-pressing
All 64 game files are byte-identical to the 1997 CD; only the autorun differs (`Skipper.exe`, `Launcher.exe`, a manual
as PDF, no Transposia catalogue any more). `SETUP.EXE` is the same Wise installer, so `start.dxr` comes out of it the
same way; the `START32.EXE` on this CD is a loader without a movie. The CD has Joliet names; the ISO9660 names under
them are upper case (`MAGNUSNL.CXT`), which the port looks up case-insensitively anyway.

## 2. The Nordic CD (Danish, Norwegian, Swedish, Finnish)
The same version 1.5 of the game as the Dutch one (Director 5, the same 20 movies), but every movie was saved again
and the text and speech casts are there four times: `MAGNUSDK.CXT`, `MAGNUSN.CXT`, `MAGNUSS.CXT`, `MAGNUSSF.CXT`, and
likewise `MMB01xx`, `MMB04xx`, `MMB09xx`, `MMB10xx` (Dutch: `MagnusNL.cxt`, `mmb01nl.cxt`, ...). `Magnus.dxr` picks
the cast with `gCurDlgLib = "Magnus" & string(gCurDlgLanguage)`.

- **Language choice**: `start.dxr` reads `MAGNUS.INI` [Language] `Speak=` and `Text=` (`DK`, `N`, `S`, `SF`; empty =
  Danish). Speech and text can be chosen separately. The Dutch `start.dxr` fixes both to `#NL`. The settings screen
  (F7, `MMDLG1.DXR`) has a row of flags for each and writes the choice back to `MAGNUS.INI`. At the first start the
  port writes the system language (`da`, `nb`/`nn`/`no`, `sv`, `fi`) into `MAGNUS.INI`, Danish otherwise; `--lang`
  always sets it.
- **Start movie**: `START32.EXE` is a plain file on the CD (the Director 5 projector with `start` inside); `SETUP.EXE`
  is a small Wise installer without a projector here. The port takes `start.dxr` from `START32.EXE` first and keeps it
  as `start_nordic.dxr` in the save folder, so it doesn't clash with the one from the Dutch CD.
- **Video**: 62 ABC videos with Danish names (`AEBLE.AVI`, `ISBJOERN.AVI`, `FLAGSVEN.AVI`, `FINSKAA.AVI`, ...) for
  the words of all four languages; the Dutch CD has 26, 7 of them only there (`GIRAF.AVI`, `UNIVERS.AVI`, ...).
- **Alphabet**: the ABC game ends on Æ Ø Å (Danish and Norwegian) or Å Ä Ö (Swedish and Finnish).
- The image is a CloneCD dump (`.img` + `.ccd` + `.sub`) with only the data track; the port reads the `.img` (raw
  2352-byte sectors) or the `.ccd` next to it.

## 3. Strings in Mac Roman
Every file was saved as a Windows file (platform 2 in VWCF), and the texts in the casts (STXT) are Windows-1252
(`ø` = `0xF8`). But the **string literals in the Lingo bytecode** are Mac Roman: the scripts were compiled on a Mac and
not recompiled afterwards. That holds for both CDs:
- Nordic: the texts of the dialogs (`"Taustaäänet"`, `"Hämta Spel"`, `"Klicka på bilden ..."`) and the alphabet of the
  ABC game (`"Ææ"`, `"Øø"`, `"Åå"`);
- Dutch: the upper/lower case table of the spelling game (MMB10) and `"Markus und Mücki"` in MMB11.

The ABC game passes `charToNum` of its letters to Windows' `VkKeyScan` to see whether they are on the keyboard, and
compares them with `the key`: that only works if the Windows projector converted the strings to Windows-1252. The
port does that when it loads a script (`mac_string` in `src/dfile.c`); before, the screen showed "TaustaŠŠnet".
Outside Windows `VkKeyScan` now also counts the accented letters as typeable (`src/xobj.c`).

## 4. The Danish CD of 1996 (Director 4)
An earlier version of the game (1.1), for Windows 3.1, in Danish only. The main movie "magnus" is inside the projector
`MAGNUS.EXE` (28 MB; the port extracts it once to `magnus_d4.dxr` in the save folder), `MAGNUS0.DXR` only jumps back
into it and `MAGNUS1.DXR` is the ending. There is no external language cast and there are no intro or dialog movies:
the game starts with its own little intro in the well, saving and loading go through a dialog in `MMSYS.DLL`, and
printing in the colouring book through `MMPRINT.DLL` with the pages as Windows metafiles (`PIC\PICn`).

What the port does for it (`DFILE_D4` in `src/dfile.c`, ScummVM's Director engine as the reference):
- **Cast members**: a 2+4-byte header with a one-byte type, data before info; scripts are linked to their members
  through the `scriptId` in the member's info (the cast id in `Lscr` is not reliable in D4).
- **Score**: 40-byte main channels (sound, tempo, palette, and the transition itself: type, duration in quarter
  seconds, chunk size, whole stage or changing area) and 20-byte sprite records; everything is in cast 1.
- **Lingo**: literals of 6 bytes (u16 type + u32), so the operands of literals, arguments and locals count in sixes;
  `the ... of cast/field` takes one argument (no castLib). Otherwise the bytecode is the same as in D5.
- **Movie settings**: version 0x45d, the default palette at offset 70.
- **XObjects**: `LINGO.INI` opens FileIO; `MMSYS.CheckCD` says yes, `LoadSaveGame` is the slot picker (Windows
  dialog, a message box on Linux/Android), `PrintMetaFile` prints the same page as the cast bitmap `PicN`.
- `the itemDelimiter` (used to read `Path=` from `MAGNUS.INI`), which the port also writes without quotes now.

Checked headless (`DATA=releases/da/files tools/regress.sh`, 17 checks): the intro, all 11 minigames, the exits,
the ABC video, printing, saving on quit and loading at the next start; fuzzed without errors. Nobody has played it
through yet.

## 5. What the other releases would need
- **Superstarter re-releases (Director 8.5 and 10)**: installer CDs (InstallShield, `DATA1.CAB`); inside, the same
  version 1.5 with the same file names (`Magnus.dxr`, `MAGNUSDK.CXT` or `MAGNUSS.cxt`, ...), saved with a newer
  Director (`DRCF`, `LctX`, Xtras for text, fonts and sound). Needed: unpacking InstallShield and the Director 8.5/10
  format (score, text as Text Xtra, new opcodes). Danish and Swedish can also be played from the Nordic CD.
