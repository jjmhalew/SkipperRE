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

## Gebruik
```bash
pip install pillow
python tools/iso.py extract                              # (pad naar SKIPPER_1.BIN staat in het script)
python tools/rifx.py extract/Magnus.dxr                  # chunks + castoverzicht
python tools/lingodis.py --out out/lingo extract/*.dxr extract/*.cxt
python tools/lingodis.py --stats extract/*.dxr extract/*.cxt
python tools/dcast.py extract/Mmb04.dxr out/cast/Mmb04   # bitmaps → PNG, tekst → TXT
```
