# Analysis

## Disc
`SKIPPER_1.BIN/.CUE` (archive.org `skipper-1`): Enhanced CD.
- Session 1: tracks 1-15 audio, lead-out 19:33:31. These are the songs for the separate player `Liedjes.exe`;
  the game itself doesn't use them (`PlayCDTrack` in Magnus.dxr is never called, the music is the `BkgSnd*`
  sounds on channel 2).
- Session 2: track 16 MODE2/2352 at 22:05:31 = file sector 99406 = absolute LBA (the 11400 sectors of the session
  gap are zeros in the BIN). User data at offset 24 of each sector (Form 1). PVD at 99422.

## Files
| File | What |
|---|---|
| `Magnus.dxr` | main movie (hub): 1277 cast members (944 bitmaps, 51 palettes, 50 sounds, 182 scripts, 11 film loops) |
| `MagnusNL.cxt` | external Dutch cast: 201 texts, 204 sounds (dialogue) |
| `Mmb01..11.dxr` | the 11 minigames; `mmbXXnl.cxt` = the Dutch speech for them |
| `Mmdlg1..5.dxr` | dialog windows (Movie In A Window, `window "DlgWin"`, `tell ... EndDialog`) |
| `Intro.dxr`, `Magnus1.dxr`, `Mmtutor.dxr` | intro, extra movie (the ending), tutorial |
| `SETUP.EXE` | 16-bit Wise installer (doesn't run on Win11 x64). Payload: `start32.exe` (Director 5 projector, 32-bit, stream 15), `start16.exe` (stream 16) |
| `DLLGLUE.DLL`, `FILEIO.DLL`, `INI.DLL`, `MOVUTILS.DLL` | 16-bit XObjects; `POMLITE.X32` = PrintOMatic Lite Xtra |
| `Video\*.avi` | 26 Cinepak clips (320x240, 15 fps, 8-bit PCM) for the ABC game (MMB09), linked `digitalVideo` members |
| `Catalog\`, `VFW\` | not used by the game (Transposia catalogue with demos, Video for Windows) |

All movies: codec `MV93`/`MC95`, `VWCF` directorVersion 1217 = **Director 5**.

## Lingo
596 scripts, 1560 handlers. Encoding: an opcode ≥ 0x40 has an argument of 1 (0x40-0x7f), 2 (0x80-0xbf)
or 4 bytes (0xc0-); `pushcons` refers to the literal table with a **byte offset** (8 bytes per record,
so index = arg / 8). Lscr header: handler vectors from offset 50, handler records of 42 bytes.

Builtins used (82): `return point updateStage script string cast puppetSound count getAt go member
length add intersect rect soundBusy voidp getaProp puppetSprite random objectp abs pass puppetTransition
getPos deleteOne symbolp startTimer charToNum addProp spriteBox rollOver put puppetPalette dontPassEvent
preLoadCast unLoadCast nothing inside cursor ilk preLoad setaProp stringp deleteAt preloadMember value float
offset numToChar setAt getOne integer deleteProp sound addAt closeXLib append constrainH unloadMember
clearGlobals openXLib getProp getPropAt pause window moveToFront open close forget continue do sprite
duplicate getLast xtra setDocumentName setMargins setLandscapeMode print castLib erase`.

Movie properties: `the stage`, `the actorList` (objects with stepFrame), `the framePalette`, `the searchPath`.

### Native calls
Through the XObject `DLLGlue(mNew, the pathName & "MMSYS.DLL", <function>, <ret>, <args>)`:
`CDPlayTrack(I)`, `CDPlaying()`, `CDStop()`, `LoadSaveGame(W,I)` (all four defined but unused: saving and loading
go through the game's own dialog movie `mmdlg3` and `MMSAVn.MMS`), plus `KEYBOARD.VkKeyScan` and
`USER.InvalidateRect/UpdateWindow`. `MMSYS.DLL` is not on the CD (it comes from the installer). Further `MAGNUS.INI`
(settings), `KEYBOARD.DLL`, `MOVUTILS`, PrintOMatic (printing).

## Bitmap cast member (spec, big-endian)
`u16 pitch|0x8000`, `rect top,left,bottom,right`, 8 bytes (unknown), `i16 regY, regX` (absolute, same coordinate
system as rect), `u8 ?`, `u8 bpp`, `i16 clutCastLib, clutMember`. BITD: raw when length = pitch×h, otherwise
PackBits per byte (n<0x80: n+1 literal; otherwise 257-n repeats). CLUT: 6 bytes per colour (16-bit RGB).

## Score (VWSC, D5)
Header (big-endian): `u32 stream size, u32 offset of frame 1 (20), u32 #frames, u16 version (7), u16 sprite size (24),
u16 #channels (50), u16 ?`. Per frame `u16 length` + deltas `(u16 len, u16 offset, data)` on a buffer of
50x24 bytes; each frame starts from the state of the previous one.
- Main channels (bytes 0-47): `0 script (lib,member)`, `4 sound1`, `8 sound2`, `12 transition`, `21 tempo`,
  `24 palette (lib,member)`, `28 palette speed`, `29 palette flags`.
- Sprite n at `48 + (n-1)*24`: `u8 type, u8 ink (|0x40 trails, |0x80 stretch), u16 castLib, u16 member,
  u16 scriptLib, u16 scriptMember, u8 fore, u8 back, i16 locV, i16 locH, i16 height, i16 width, ...`.
  loc = registration point; top left = loc - (regX - rect.left, regY - rect.top).
- VWLB: `u16 n`, n+1 × `(u16 frame, u16 offset)`, then the names one after another.
- MCsL: list with per cast (name, path, preload, (min, max, id)).
- The game itself sets nearly all sprites from Lingo (`InitSprite`/`SetSpriteCast`, puppets): the scores only
  hold placeholders at (-100,-100). Only the start movie has a 'real' score.

## Start chain
`start32.exe` contains (APPL RIFX after the `59JP` header, File chunk with absolute offsets) the movie **start**
(`tools/projector.py` → `extract/start.dxr`): open INI/MOVUTILS/FILEIO/DLLGLUE, `IVANOFF.INI` [MAGNUS]
Path → `gMMPath`, `MAGNUS.INI` [Sound] StartLevel → `the soundLevel`, CD check via `gCDDrive & ":\MMTUTOR.DXR"`
(`gCDDrive` comes from `LINGO.INI`, filled in by the installer), colour depth 8 → `go 1, "INTRO"` →
`go "Start", "MAGNUS"`.

## Decompiler
`tools/lingodec.py` makes readable Lingo (if/else, repeat while, repeat with ... in, case). All movies:
`python tools/lingodec.py --out out/src extract/*.dxr extract/*.cxt` (~24,000 lines, 13 leftovers).
Semantics that follow from it: `get/set` with type 6 = sprite property, 7 = "the" animation property,
9 = member property (lib, member from the stack), 0 = movie property (mouseDownScript, ...).
Locals/args/literals: index × 8. `objcallv4 t` calls the variable (type t) as an XObject.
