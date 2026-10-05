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
| Danish, first release 1996 (`MAGNUS11`) | `magnus-myggen-leg-og-laer` | `Magnus-Myggen-Leg-og-Laer.iso` `27f69c23…` | 4 (1117) | no |
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

## 4. What the other releases would need
- **Danish 1996 (Director 4)**: an earlier version of the game, for Windows 3.1. The main movie is inside the projector
  `MAGNUS.EXE` (28 MB), there are `MAGNUS0.DXR` and `MAGNUS1.DXR` instead of `Magnus.dxr`, no external language cast,
  no intro and dialog movies, the colouring pages as separate `PIC\PICn` images. Needed: Director 4 files (different
  cast, score and script layout) and the projector as a movie. The Danish version can also be played from the Nordic
  CD.
- **Superstarter re-releases (Director 8.5 and 10)**: installer CDs (InstallShield, `DATA1.CAB`); inside, the same
  version 1.5 with the same file names (`Magnus.dxr`, `MAGNUSDK.CXT` or `MAGNUSS.cxt`, ...), saved with a newer
  Director (`DRCF`, `LctX`, Xtras for text, fonts and sound). Needed: unpacking InstallShield and the Director 8.5/10
  format (score, text as Text Xtra, new opcodes). Danish and Swedish can also be played from the Nordic CD.
