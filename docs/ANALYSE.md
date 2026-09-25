# Analyse

## Disc
`SKIPPER_1.BIN/.CUE` (archive.org `skipper-1`): Enhanced CD.
- Sessie 1: tracks 1-15 audio, lead-out 19:33:31. Dit zijn de liedjes voor de losse speler `Liedjes.exe`;
  het spel zelf gebruikt ze niet (`PlayCDTrack` in Magnus.dxr wordt nergens aangeroepen, de muziek zijn
  de `BkgSnd*`-geluiden op kanaal 2).
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
`CDPlayTrack(I)`, `CDPlaying()`, `CDStop()`, `LoadSaveGame(W,I)` (alle vier gedefinieerd maar niet gebruikt:
opslaan/laden gaat via de eigen dialoogfilm `mmdlg3` en `MMSAVn.MMS`), plus `KEYBOARD.VkKeyScan` en
`USER.InvalidateRect/UpdateWindow`. `MMSYS.DLL` staat niet op de CD (komt uit de installer). Verder `MAGNUS.INI` (instellingen), `KEYBOARD.DLL`, `MOVUTILS`, PrintOMatic (printen).

## Bitmap-castlid (spec, big-endian)
`u16 pitch|0x8000`, `rect top,left,bottom,right`, 8 bytes (onbekend), `i16 regY, regX` (absoluut, zelfde
stelsel als rect), `u8 ?`, `u8 bpp`, `i16 clutCastLib, clutMember`. BITD: rauw als lengte = pitch×h, anders
PackBits per byte (n<0x80: n+1 letterlijk; anders 257-n herhalingen). CLUT: 6 bytes per kleur (16-bit RGB).

## Score (VWSC, D5)
Kop (big-endian): `u32 streamgrootte, u32 offset frame 1 (20), u32 #frames, u16 versie (7), u16 spritegrootte (24),
u16 #kanalen (50), u16 ?`. Per frame `u16 lengte` + delta's `(u16 len, u16 offset, data)` op een buffer van
50x24 bytes; elk frame begint met de toestand van het vorige.
- Hoofdkanalen (bytes 0-47): `0 script (lib,member)`, `4 sound1`, `8 sound2`, `12 transitie`, `21 tempo`,
  `24 palet (lib,member)`, `28 paletsnelheid`, `29 paletvlaggen`.
- Sprite n op `48 + (n-1)*24`: `u8 type, u8 ink (|0x40 trails, |0x80 stretch), u16 castLib, u16 member,
  u16 scriptLib, u16 scriptMember, u8 fore, u8 back, i16 locV, i16 locH, i16 hoogte, i16 breedte, ...`.
  loc = registratiepunt; linksboven = loc - (regX - rect.left, regY - rect.top).
- VWLB: `u16 n`, n+1 × `(u16 frame, u16 offset)`, daarna de namen achter elkaar.
- MCsL: lijst met per cast (naam, pad, preload, (min, max, id)).
- Het eigenlijke spel zet vrijwel alle sprites vanuit Lingo (`InitSprite`/`SetSpriteCast`, puppets): in de
  scores staan alleen placeholders op (-100,-100). Alleen de startfilm heeft een 'echte' score.

## Opstartketen
`start32.exe` bevat (APPL-RIFX achter de `59JP`-kop, File-chunk met absolute offsets) de film **start**
(`tools/projector.py` → `extract/start.dxr`): INI/MOVUTILS/FILEIO/DLLGLUE openen, `IVANOFF.INI` [MAGNUS]
Path → `gMMPath`, `MAGNUS.INI` [Sound] StartLevel → `the soundLevel`, CD-check via `gCDDrive & ":\MMTUTOR.DXR"`
(`gCDDrive` komt uit `LINGO.INI`, door de installer ingevuld), kleurdiepte 8 → `go 1, "INTRO"` →
`go "Start", "MAGNUS"`.

## Decompiler
`tools/lingodec.py` maakt leesbare Lingo (if/else, repeat while, repeat with ... in, case). Alle films:
`python tools/lingodec.py --out out/src extract/*.dxr extract/*.cxt` (~24.000 regels, 13 restanten).
Semantiek die daaruit blijkt: `get/set` met type 6 = spriteproperty, 7 = "the"-animatieproperty,
9 = member-property (lib, member van de stack), 0 = movie-property (mouseDownScript, ...).
Locals/args/literals: index × 8. `objcallv4 t` roept de variabele (type t) aan als XObject.
