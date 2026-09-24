# Analyse

## Disc
`SKIPPER_1.BIN/.CUE` (archive.org `skipper-1`): Enhanced CD.
- Sessie 1: tracks 1-15 audio (spelmuziek, via `CDPlayTrack`), lead-out 19:33:31.
- Sessie 2: track 16 MODE2/2352 op 22:05:31 = bestandssector 99406 = absolute LBA (de 11400 sectoren
  sessiegat staan als nullen in de BIN). Userdata op offset 24 van elke sector (Form 1). PVD op 99422.

## Bestanden
| Bestand | Wat |
|---|---|
| `Magnus.dxr` | hoofdfilm (hub): 1277 castleden (944 bitmaps, 51 paletten, 50 geluiden, 182 scripts, 11 filmloops) |
| `MagnusNL.cxt` | externe cast NL: 201 teksten, 204 geluiden (dialogen) |
| `Mmb01..11.dxr` | de 11 spelletjes; `mmbXXnl.cxt` = NL-spraak daarbij |
| `Mmdlg1..5.dxr` | dialoogvensters (Movie In A Window, `window "DlgWin"`, `tell ... EndDialog`) |
| `Intro.dxr`, `Magnus1.dxr`, `Mmtutor.dxr` | intro, extra film, tutorial |
| `SETUP.EXE` | 16-bit Wise-installer (draait niet op Win11 x64). Payload: `start32.exe` (Director 5 projector, 32-bit, stroom 15), `start16.exe` (stroom 16) |
| `DLLGLUE.DLL`, `FILEIO.DLL`, `INI.DLL`, `MOVUTILS.DLL` | 16-bit XObjects; `POMLITE.X32` = PrintOMatic Lite Xtra |
| `Video\*.avi`, `Catalog\`, `VFW\` | ongebruikt door het spel (Deense video's, Transposia-catalogus, Video for Windows) |

Alle films: codec `MV93`/`MC95`, `VWCF` directorVersion 1217 = **Director 5**.

## Lingo
596 scripts, 1560 handlers. Codering: opcode ≥ 0x40 heeft een argument van 1 (0x40-0x7f), 2 (0x80-0xbf)
of 4 bytes (0xc0-); `pushcons` verwijst met een **byte-offset** in de literaltabel (8 bytes per record,
dus index = arg / 8). Lscr-kop: handler-vectoren vanaf offset 50, handlerrecords van 42 bytes.

Gebruikte builtins (82): `return point updateStage script string cast puppetSound count getAt go member
length add intersect rect soundBusy voidp getaProp puppetSprite random objectp abs pass puppetTransition
getPos deleteOne symbolp startTimer charToNum addProp spriteBox rollOver put puppetPalette dontPassEvent
preLoadCast unLoadCast nothing inside cursor ilk preLoad setaProp stringp deleteAt preloadMember value float
offset numToChar setAt getOne integer deleteProp sound addAt closeXLib append constrainH unloadMember
clearGlobals openXLib getProp getPropAt pause window moveToFront open close forget continue do sprite
duplicate getLast xtra setDocumentName setMargins setLandscapeMode print castLib erase`.

Movie-properties: `the stage`, `the actorList` (objecten met stepFrame), `the framePalette`, `the searchPath`.

### Native aanroepen
Via het XObject `DLLGlue(mNew, the pathName & "MMSYS.DLL", <functie>, <ret>, <args>)`:
`CDPlayTrack(I)`, `CDPlaying()`, `CDStop()`, `LoadSaveGame(W,I)`. `MMSYS.DLL` staat niet op de CD
(komt uit de installer). Verder `MAGNUS.INI` (instellingen), `KEYBOARD.DLL`, `MOVUTILS`, PrintOMatic (printen).

## Bitmap-castlid (spec, big-endian)
`u16 pitch|0x8000`, `rect top,left,bottom,right`, 8 bytes (onbekend), `i16 regY, regX` (absoluut, zelfde
stelsel als rect), `u8 ?`, `u8 bpp`, `i16 clutCastLib, clutMember`. BITD: rauw als lengte = pitch×h, anders
PackBits per byte (n<0x80: n+1 letterlijk; anders 257-n herhalingen). CLUT: 6 bytes per kleur (16-bit RGB).
