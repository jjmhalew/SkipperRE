# SkipperRE

Reverse engineering van *Skipper & Skeeto in Pretpark* (PC, Transposia 1997; NL-versie van het Deense
*Magnus og Myggen*), met als doel een native Windows 11-speler die de originele data (`*.dxr`, `*.cxt`,
de CD-audiotracks) inlaadt.

Het spel is gemaakt met **Macromedia Director 5** (bestandsversie 1217). Er is dus geen spelcode in een exe:
alle logica zit als Lingo-bytecode in de `.dxr`-films. De port is daarom een eigen Director 5-runtime
(RIFX-container, cast, score, Lingo-VM) plus native vervangers voor de paar DLL's die het spel aanroept.

Er staan geen game-bestanden in deze repo. Zet de inhoud van de datatrack in `extract/` en de uit de
Wise-installer gehaalde `start32.exe` in `game/` (beide staan in `.gitignore`).

## Stand van zaken
- **Disc ontleed**: Enhanced CD, sessie 1 = 15 audiotracks (muziek), sessie 2 = ISO9660-datatrack
  (LBA 99406). `tools/iso.py` haalt de bestanden uit `SKIPPER_1.BIN` → [docs/ANALYSE.md](docs/ANALYSE.md)
- **RIFX-container** (`tools/rifx.py`): imap/mmap/KEY*/CAS*/CASt, little-endian XFIR
- **Lingo-disassembler** (`tools/lingodis.py`): alle 596 scripts / 1560 handlers, 0 onbekende opcodes;
  `--stats` geeft het gebruik van builtins (82 stuks), properties en entities
- **Bitmaps + paletten** (`tools/dcast.py`): 1/8 bpp, RLE, CLUT-leden → PNG
- **Decompiler** (`tools/lingodec.py`): leesbare Lingo van alle films
- **Score, labels, cast-libs** (`tools/score.py`), frame renderen met inks 0/8/36
- **Startfilm uit de projector** (`tools/projector.py`)
- **Native engine in C** (`src/`): eigen Director 5-runtime die de originele films afspeelt. De hele
  opstartketen draait (start → INTRO → Magnus), Skipper's kamer met Skipper en Skeeto, dialogen met
  ondertitels, en de minigames openen vanuit de boekenplank (MMB09 getest).

## Native engine
| Bestand | Wat |
|---|---|
| `src/dfile.c` | RIFX-container, interne en externe casts (per eigenaar-id), bitmaps, `snd `, STXT, scripts, score |
| `src/lingo.c` | waarden (refcounted), symbolen, globals, de bytecode-VM, objecten met ancestor-keten |
| `src/builtins.c` | lijsten, proplijsten, strings, rekenen, types |
| `src/player.c` | films, frames, events (sprite → castlid → frame → movie), puppets, sprite-/member-properties, `go`, paletten, transities, MIAW-vensters |
| `src/stage.c` | compositie naar 32-bit met inks (copy, matte, bg transparent, blend, ...), tekst via GDI, vormen, filmloops |
| `src/xobj.c` | INI, FileIO, MovUtils, DLLGlue → MMSYS.DLL (CD-audio, LoadSaveGame), KEYBOARD.DLL, USER.EXE |
| `src/video.c` | digitale video: AVI-parser en eigen Cinepak-decoder (ABC-spel), geluidsspoor via de mixer |
| `src/sound.c` | waveOut-mixer, CD-audio rechtstreeks uit `SKIPPER_1.BIN` via de `.CUE` |
| `src/main.c` | Win32-venster, timing, invoer (Mac-keyCodes), cursors, headless testmodus, crash-handler |
| `src/dbgheap.c` | debug-heap (`DEFS=-DDBGHEAP`): canaries + quarantaine, meldt bestand:regel |

Semantiek die uit het spel bleek (en die de port nodig had):
- `birth(script "X")` / `new` is altijd de constructor, ook als het aanroepende script zelf `birth` heeft.
- Een script of object als eerste argument krijgt de aanroep (`Event(script "LocScriptC5", ...)`),
  ook bij een lokale aanroep (`mouseUp(script "HSC5ABC")` vanuit een script met eigen `mouseUp`).
- `objcallv4` op een naam: het eerste argument (symbool) is de naam van een variabele (local/arg/property).
- Hotspots zijn vormen met ink 36 en fore = back: onzichtbaar maar klikbaar.
- Filmloop-sprites met castLib -1 verwijzen naar de cast van de loop zelf.
- `objcallv4` op een naam valt terug op een bestaande global, ook als die in de handler niet
  gedeclareerd is: `add(gAnimNotify, ...)` (alle AnimEnd-events) en `symbolp(gEffectNotify)` (uitgangen
  dicht tijdens een geluidseffect) werken alleen zo.
- Het sprite-script komt ook bij puppet-sprites uit het huidige frame (Magnus frame 3 zet het per
  ongeluk op kanaal 10; in frame 4 staat het op 11, de brievenbus).
- `moveableSprite`: Director sleept de sprite zelf; bij loslaten ziet `mouseUp` de nieuwe positie.
- Klikken: alleen matte- en mask-ink testen per pixel; bij background transparent telt de hele rechthoek.
- Editable velden (textFlags bit 0): toetsen gaan eerst naar het sprite-script van het veld; alleen wat
  niet afgevangen wordt (of `pass` doet) komt in het veld. `set the textFont of field` is get/set-type 11.
- Digitale video: registratiepunt in het midden; `movieTime`/`duration` in ticks.

```bash
./build.sh                                   # out/skipper.exe (debug-build met UBSan + PDB)
OPT=-O0 DEFS=-DDBGHEAP ./build.sh            # met debug-heap
./out/skipper.exe extract                    # spelen (venster 2x, geluid; de .BIN is niet nodig)
./out/skipper.exe extract --bin D:/pad/SKIPPER_1.BIN --scale 1
# headless testen: N frames draaien, klikken/slepen, periodiek screenshots, globals/kanalen dumpen
./out/skipper.exe extract --click 320 240 170 --click 400 100 300 --shot 700 out/run --every 50 --dump
./out/skipper.exe extract --click 320 240 170 --click 320 393 300 --drag 100 350 150 450 420 --shot 520 out/coin --dump
python tools/filmstrip.py out/run out/strip.png 6
```
Opslag (INI's, spelposities): `%APPDATA%\SkipperRE`.

## Gebruik
```bash
pip install pillow
python tools/iso.py extract                              # (pad naar SKIPPER_1.BIN staat in het script)
python tools/rifx.py extract/Magnus.dxr                  # chunks + castoverzicht
python tools/lingodis.py --out out/lingo extract/*.dxr extract/*.cxt
python tools/lingodis.py --stats extract/*.dxr extract/*.cxt
python tools/dcast.py extract/Mmb04.dxr out/cast/Mmb04   # bitmaps → PNG, tekst → TXT
```
