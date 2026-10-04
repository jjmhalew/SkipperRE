# SkipperRE

Een nieuwe speler voor *Skipper & Skeeto in Pretpark* (PC, Transposia 1997; NL-versie van het Deense
*Magnus og Myggen*), gebouwd door het origineel te ontleden. Hij draait de originele spelbestanden van **je eigen
cd** op Windows 10/11, Linux (ook de Steam Deck) en Android. Er zitten geen spelbestanden in deze repository of in
de releases.

> Onofficieel fanproject, voor het bewaren van een oud kinderspel. Niet verbonden aan of goedgekeurd door Transposia,
> Ivanoff Interactive of de makers van Skipper & Skeeto; namen en merken zijn van hun eigenaars. Je hebt je eigen
> exemplaar van het spel nodig.

## Spelen
**Windows**: download de `windows.zip` van de [Releases](../../releases)-pagina (of bouw hem zelf, zie onder), pak hem
uit in een eigen map en start `skipper.exe`. (De exe is niet ondertekend; zegt SmartScreen "Windows heeft uw pc
beschermd": *Meer informatie*, dan *Toch uitvoeren*.) Hij zoekt de spelbestanden zelf:
1. de map die je opgeeft (`skipper.exe <map>`), of `extract\` / `data\` naast de exe;
2. `%APPDATA%\SkipperRE\data` (een eerder uitgepakte cd);
3. een cd-station met de cd erin;
4. een cd-image naast de exe of in de werkmap (`.cue` of `.iso`), of `--bin <SKIPPER_1.BIN, .CUE of .ISO>`;
5. anders vraagt hij erom: kies het image (`.cue`, `.bin` of `.iso`) of `Magnus.dxr` op de cd of in een map met
   de bestanden van de cd.

Een image wordt eenmalig uitgepakt naar `%APPDATA%\SkipperRE\data` (~200 MB); daarna is de cd niet meer nodig. Ook
een `.bin` zonder `.cue` werkt (de datatrack wordt opgezocht).

**Linux en Steam Deck**: download de `linux-x86_64.tar.gz` (of `./build.sh`), pak hem uit en start `skipper`. Nodig:
SDL2 (`libsdl2-2.0-0`; de meeste desktops en SteamOS hebben het). Zoeken gaat als op Windows, met de cd onder
`/media`, `/run/media` of `/mnt`; uitpakken naar `~/.local/share/SkipperRE/data`. Zonder iets gevonden vraagt hij
erom met `zenity` of `kdialog`. Op de Steam Deck: voeg `skipper` toe als niet-Steam-spel.

**Android** (7.0 of nieuwer, 64-bit): installeer `SkipperRE-<versie>.apk` (sta installeren uit je browser of
bestandsbeheer toe als Android erom vraagt). Bij de eerste start kies je in Androids bestandskiezer het image van de
cd (`SKIPPER_1.BIN` of een `.iso`) of een map met een kopie van de cd; de bestanden komen in de app
(`Android/data/io.github.jjmhalew.skipperre/files/data`). Opgeslagen spellen en `skipper.log` staan in
`Android/data/io.github.jjmhalew.skipperre/files`.

Heb je al een opgeslagen spel, dan slaat de start het logo en de intro over en opent het spel meteen het scherm
om een spel te kiezen. `--intro` speelt ze toch, `--nointro` slaat ze altijd over.

### Besturing
| | Muis / toetsenbord | Controller (DualSense, DualShock 4, Xbox, ...) | Aanraken (Android) |
|---|---|---|---|
| Aanwijzen | muis | linker of rechter stick (d-pad: langzaam en precies) | |
| Klikken, slepen | linkerknop | A / Kruis (of de touchpad-klik); vasthouden = slepen | tikken, slepen |
| Spel laden / opslaan | F5 / F6 | X / Vierkant, Y / Driehoek | knoppen *Laden*, *Opslaan* |
| Uitleg | F1 | Back / Share | *Uitleg* |
| Ondertitels aan/uit | F2 | LB / L1 | *Tekst* |
| Achtergrondmuziek aan/uit | F3 | RB / R1 | *Muziek* |
| Zachter / harder | pijl omlaag / omhoog | LT / L2, RT / R2 | *Zachter*, *Harder* |
| Dialoog dicht, stoppen | Esc | B / Rondje of Start / Options | *Stoppen*, terugknop |
| Volledig scherm | Alt+Enter, F11 (Linux) | | |

Controllers werken via USB en Bluetooth zonder extra software (op Windows: DualSense en DualShock 4 rechtstreeks,
de rest via XInput; op Linux en Android via SDL). De aanwijzer volgt de stick; op Android tekent het spel hem zelf.
Op een telefoon staan de knoppen in de zwarte balken naast het beeld, op een 4:3-tablet achter de knop *Menu*.

### Texture packs
Elk plaatje van het spel kan vervangen worden door een PNG van willekeurige grootte, bijvoorbeeld een HD-versie.
Start het spel met `--dumptex` en speel: elk plaatje dat het spel laat zien komt in
`<opslagmap>\mods\dump\<film>\<naam>_<b>x<h>_<hash>.png`. Bewerk of vergroot een bestand, houd het einde
`_<hash>.png` (wat ervoor staat mag alles zijn) en zet het in `mods\textures\` (submappen mogen) in de opslagmap of
naast het programma. Is een plaatje groter (2x, 3x, 4x), dan tekent de speler het hele beeld in die resolutie
(`--hd N` kiest zelf). Doorzichtigheid komt van het originele plaatje, dus een pakket hoeft alleen kleuren te leveren;
pixels met alfa < 128 zijn ook doorzichtig. Opslagmap: `%APPDATA%\SkipperRE`, `~/.local/share/SkipperRE`, of op
Android `Android/data/io.github.jjmhalew.skipperre/files`.

## Bouwen
- **Windows** (10 of 11, vooraf niets te installeren): dubbelklik `build.bat` → `out\skipper.exe`. De C-compiler is
  [Zig](https://ziglang.org): een `zig` op PATH of `pip install ziglang` wordt gebruikt als die er is, anders downloadt
  `build.bat` eenmalig de officiële Zig 0.16.0 voor Windows (ongeveer 95 MB) naar `tools\zig` en controleert de
  SHA-256. `build.bat dev` bouwt de ontwikkelversie `out\dev.exe` (log in de console, UBSan). In Git Bash kan ook
  `./build.sh` (zie hieronder).
- **Eén exe voor jezelf**: dubbelklik `make_standalone.bat` → `SkipperRE-standalone.exe` (ongeveer 165 MB), met
  `skipper.exe` en **jouw** spelbestanden erin: draait zonder cd, image of `data`-map. De spelbestanden komen uit
  `extract\`, `data\` of `%APPDATA%\SkipperRE\data` (een eerder uitgepakt image), of uit de map die je opgeeft, zoals
  het cd-station: `make_standalone.bat D:\`. Hij bevat het spel zelf, dus alleen voor eigen gebruik: nooit delen of
  uploaden.
- **Linux**: `./build.sh` → `out/skipper` (Debian / Ubuntu: `sudo apt install build-essential libsdl2-dev`).
- **Android**: `cd android` en `./gradlew assembleRelease` (of `assembleDebug`) → `android/app/build/outputs/apk/`.
  Nodig: Android SDK met NDK 27.2 en CMake 3.22 (Android Studio kan de map `android` ook openen). De build haalt
  SDL 2.32.10 op en controleert de SHA-256. Zonder eigen sleutel (`-PskipperKeystore=... -PskipperKeyAlias=...
  -PskipperKeyPassword=...`) krijgt de release-APK de debug-sleutel; prima om zelf te installeren.
- **CI**: `.github/workflows/build.yml` bouwt alle drie bij elke push; een tag `v1.2.3` (gelijk aan de versie in
  `res/skipperre.rc`) maakt een GitHub-release met de zip, de tar.gz en de APK. Voor een vaste Android-sleutel:
  de secrets `ANDROID_KEYSTORE_B64`, `ANDROID_KEY_ALIAS` en `ANDROID_KEY_PASSWORD`.

## Licentie
De code in deze repository valt onder de [GNU General Public License v3.0](LICENSE) of later. Dat geldt niet voor
het originele spel, de spelbestanden of de merken; die zijn van hun eigenaars en geen deel van dit project.
`src/stb/` bevat stb_image, stb_image_write en stb_truetype van Sean Barrett (public domain of MIT, zie het eind van
elk bestand); `res/fonts/` de Liberation-lettertypen (SIL Open Font License, `res/fonts/LICENSE`);
`android/app/src/main/java/org/libsdl/` is SDL's Android-code (SDL 2.32.10, zlib-licentie). `src/pad.c` en de
Android-opzet komen uit [WoodyRE](https://github.com/jjmhalew/WoodyRE).

# Ontwikkeling en reverse engineering
Het spel is gemaakt met **Macromedia Director 5** (bestandsversie 1217). Er is dus geen spelcode in een exe:
alle logica zit als Lingo-bytecode in de `.dxr`-films. De port is daarom een eigen Director 5-runtime
(RIFX-container, cast, score, Lingo-VM) plus native vervangers voor de paar DLL's die het spel aanroept.

De opstartfilm `start.dxr` staat niet los op de cd: die zit in de projector `start32.exe`, als deflate-stroom
in de Wise-installer `SETUP.EXE`. De speler haalt hem daar eenmalig uit (`src/disc.c`: eigen inflate en
ISO9660-lezer). Voor de Python-tools kun je nog steeds `extract/` en `game/` handmatig vullen (`.gitignore`).

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
| `src/stage.c` | compositie naar 32-bit met inks (copy, matte, bg transparent, blend, ...), vormen, filmloops; op schaal 1-4 (texture packs) |
| `src/text_gdi.c`, `src/text_ttf.c` | tekstmembers: GDI op Windows, stb_truetype + Liberation Sans/Mono elders |
| `src/texpack.c` | texture packs: `--dumptex`, `mods/textures`, HD-vervangingen |
| `src/xobj.c` | INI, FileIO, MovUtils, DLLGlue → MMSYS.DLL (CD-audio, LoadSaveGame), KEYBOARD.DLL, USER.EXE |
| `src/ini.c` | INI-bestanden zoals Windows' GetPrivateProfileString/WritePrivateProfileString (op elk platform) |
| `src/video.c` | digitale video: AVI-parser en eigen Cinepak-decoder (ABC-spel), geluidsspoor via de mixer |
| `src/pack.c` | spelbestanden achter de exe (één exe, zie hieronder); echte bestanden gaan voor |
| `src/disc.c` | spelbestanden vinden, BIN/CUE/ISO uitpakken (ook een BIN zonder CUE), `start.dxr` uit `SETUP.EXE` |
| `src/sound.c` | mixer (waveOut op Windows, SDL elders), CD-audio rechtstreeks uit `SKIPPER_1.BIN` via de `.CUE` |
| `src/main.c` | start, hoofdlus, invoerwachtrij, transities, opening overslaan, headless testmodus (virtuele klok) |
| `src/host_win.c`, `src/host_sdl.c` | venster, invoer, cursors, meldingen, bestandskiezer, afdrukken: Win32 / SDL2 |
| `src/plat_win.c`, `src/plat_posix.c` | bestanden, mappen, tijd, mutex (`src/plat.h`); buiten Windows paden zonder hoofdlettergevoeligheid |
| `src/pad.c`, `src/pad_sdl.c`, `src/padinput.c` | controllers (raw HID / XInput op Windows, SDL elders) als muis en sneltoetsen |
| `src/android.c` | Android: eerste start (bestandskiezer), meldingen, aanraakknoppen |
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
RELEASE=1 ./build.sh                         # geoptimaliseerd, zonder UBSan (om te spelen)
OPT=-O0 DEFS=-DDBGHEAP ./build.sh            # met debug-heap
./out/skipper.exe extract                    # spelen (venster zo groot als past; Alt+Enter = volledig scherm)
./out/skipper.exe extract --fullscreen       # meteen volledig scherm; --scale N voor een vaste venstergrootte
# headless testen: N frames draaien, klikken/slepen, periodiek screenshots, globals/kanalen dumpen
./out/skipper.exe extract --click 320 240 170 --click 400 100 300 --shot 700 out/run --every 50 --dump
./out/skipper.exe extract --click 320 240 170 --click 320 393 300 --drag 100 350 150 450 420 --shot 520 out/coin --dump
python tools/filmstrip.py out/run out/strip.png 6
sh tools/regress.sh                          # regressietest: 11 minigames + brievenbus/munt/baas/opslaan/emmer
```
Opslag (INI's, spelposities): `%APPDATA%\SkipperRE`.

### Eén exe
```bat
make_standalone.bat [map]                    :: = build.bat standalone -> SkipperRE-standalone.exe (~165 MB)
out\pack.exe [exe] [datamap] [uit.exe]       :: alleen inpakken (tools\pack.c; build.bat standalone bouwt hem)
```
`tools\pack.c` plakt de films, casts, video's en `start.dxr` (raw deflate, 195 → 162 MB) achter de exe;
de engine ziet ze als bestanden in de map van de exe (`src/pack.c`). Alleen voor eigen gebruik: de spelbestanden
zijn auteursrechtelijk beschermd.

### Eigen spellingboeken (spellingspel, MMB10)
Het originele `STAVEDIT.EXE` is 16-bit en draait niet op Windows 11, maar het formaat is simpel. Zet in
`%APPDATA%\SkipperRE` een tekstbestand `GBBOOK1.MMB` (t/m `GBBOOK4.MMB`, de drie kleine boekjes op de plank):
```
E
kat,kat.wav
hond,hond.wav
```
Regel 1: `H` = moeilijk, iets anders = makkelijk. Daarna per regel `woord,geluid.wav` (max. 200 woorden, 20
letters). De WAV's (PCM 8/16-bit) staan in `%APPDATA%\SkipperRE\WAV\`.

## Gebruik
```bash
pip install pillow
python tools/iso.py extract                              # (pad naar SKIPPER_1.BIN staat in het script)
python tools/rifx.py extract/Magnus.dxr                  # chunks + castoverzicht
python tools/lingodis.py --out out/lingo extract/*.dxr extract/*.cxt
python tools/lingodis.py --stats extract/*.dxr extract/*.cxt
python tools/dcast.py extract/Mmb04.dxr out/cast/Mmb04   # bitmaps → PNG, tekst → TXT
```
