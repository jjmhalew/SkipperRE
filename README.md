# SkipperRE

A new player for *Skipper & Skeeto in Pretpark* (PC, Transposia 1997; the Dutch version of the Danish
*Magnus og Myggen*), rebuilt by reverse engineering the original. It runs the original game files from **your own
CD** on Windows 10/11, Linux (including the Steam Deck), Android and the Nintendo Switch (homebrew). No game files are included in this repository or
its releases.

> Unofficial fan project, to preserve an old children's game. Not affiliated with or endorsed by Transposia, Ivanoff
> Interactive or the makers of Skipper & Skeeto; names and trademarks belong to their owners. You need your own copy
> of the game.

**Supported CDs** (details in [docs/RELEASES.md](docs/RELEASES.md)):
- the Dutch CD *Skipper & Skeeto in Pretpark* (1997), and its 2003 re-pressing (the same game files);
- the Nordic CD *Magnus & Myggen* (Ivanoff Interactive), with speech and text in four languages: Danish (*Magnus og
  Myggen*), Norwegian (*Magnus & Myggen*), Swedish (*Magnus och Myggan*) and Finnish (*Manu ja Matti*). At the first
  start the game picks your system's language (Danish otherwise); after that you switch in the game itself, with the
  flags in the settings screen (F7), separately for speech and text. `--lang DK|N|S|SF` picks it from the command line;
- the first Danish CD *Magnus og Myggen* (1996, Director 4), an earlier version of the game in Danish only. It asks
  for a saved game at every start and saves when you quit (Esc), in a small slot picker, as the original did.

Not supported: the later re-releases on an installer CD (*Superstarter*, Director 8.5 and 10).

The player's own messages (file picker, errors) are in English, or in Dutch on a Dutch system.

> **Nederlands:** SkipperRE speelt de cd *Skipper & Skeeto in Pretpark* (1997, ook de herpersing van 2003). Download
> voor Windows de `windows.zip` van de [Releases](../../releases)-pagina, pak hem uit in een eigen map en start
> `skipper.exe`. Hij vindt de cd in het station, of vraagt om een image van de cd (`SKIPPER_1.BIN`, `.cue` of `.iso`)
> of een map met de bestanden van de cd, en kopieert de spelbestanden eenmalig. Daarna is de cd niet meer nodig. Voor
> Linux is er een `tar.gz`, voor Android een `.apk`, voor een Switch met homebrew een `switch.zip`. De meldingen van de speler zelf zijn Nederlands op een Nederlands
> systeem. De rest van deze pagina is Engels.

## Playing
**Windows**: download the `windows.zip` from the [Releases](../../releases) page (or build it yourself, see below),
unpack it into a folder of its own and start `skipper.exe`. (The exe is not signed; if SmartScreen says "Windows
protected your PC": *More info*, then *Run anyway*.) It looks for the game files itself:
1. the folder you give it (`skipper.exe <folder>`), or `extract\` / `data\` next to the exe;
2. `%APPDATA%\SkipperRE\data` (a CD unpacked earlier);
3. a CD drive with the CD in it;
4. a CD image next to the exe or in the working folder (`.cue`, `.iso` or CloneCD `.ccd`), or
   `--bin <SKIPPER_1.BIN, .CUE, .ISO or .IMG>`;
5. otherwise it asks for one: pick the image (`.cue`, `.bin`, `.iso`, `.img` or `.ccd`) or `Magnus.dxr` on the CD or
   in a folder with the CD's files.

An image is unpacked once into `%APPDATA%\SkipperRE\data` (~200 MB); after that the CD is no longer needed. A `.bin`
without its `.cue` works too (the data track is searched for).

**Linux and Steam Deck**: download the `linux-x86_64.tar.gz` (or run `./build.sh`), unpack it and start `skipper`. It
needs SDL2 (`libsdl2-2.0-0`; most desktops and SteamOS have it). The search works as on Windows, with the CD under
`/media`, `/run/media` or `/mnt`; images are unpacked into `~/.local/share/SkipperRE/data`. If nothing is found it asks
with `zenity` or `kdialog`. On the Steam Deck: add `skipper` as a non-Steam game.

**Android** (7.0 or later, 64-bit): install `SkipperRE-<version>.apk` (allow installing from your browser or file
manager when Android asks). At the first start you pick the CD image (`SKIPPER_1.BIN`, an `.iso` or a CloneCD `.img`)
or a folder with a copy of the CD in Android's file picker; the files are copied into the app
(`Android/data/io.github.jjmhalew.skipperre/files/data`). Saved games and `skipper.log` are in
`Android/data/io.github.jjmhalew.skipperre/files`.

**Nintendo Switch** (experimental; needs a Switch that runs homebrew, i.e. custom firmware such as Atmosphère): unpack
the `switch.zip` from the Releases page onto the SD card, which gives `/switch/skipperre/skipperre.nro` (or build it
with `./build_switch.sh`). Put the CD image in the same folder: `SKIPPER_1.CUE` with `SKIPPER_1.BIN`, an `.iso`, or
the CloneCD `.ccd` with its `.img`. The first start unpacks it into `/switch/skipperre/data` (a few minutes, once),
then asks you to press + and start SkipperRE again. Keep the `.cue` and `.bin` there afterwards: the music of the CD is
read from them. Or copy all files of the CD into `/switch/skipperre/data` yourself. Start it from the Homebrew Menu,
preferably by holding R while starting a game (title takeover; the Album applet has less memory). Saved games and
`skipper.log` are in `/switch/skipperre`. The Joy-Cons (attached or held as a pair) and the Pro Controller work as in
the table below with the buttons by their labels: A clicks, B and + close a dialog or quit, X / Y load and save,
− is help, L / R subtitles and music, ZL / ZR the volume. In handheld mode you can also tap the screen; the touch
buttons then appear next to the picture until you use the Joy-Cons again.

A click, tap or key press during the logo goes straight on to the intro (the original waited for the logo's tune to
end). If you already have a saved game, the logo still plays but the intro is skipped: you go straight to the screen
for picking a saved game. `--intro` plays the intro anyway, `--nointro` skips both the logo and the intro.

`--songs` opens the Dutch CD's sing-along menu first (*Liedjes om mee te zingen*, the original `Liedjes.exe`: 15 songs
from other Transposia games, the CD's audio tracks). The songs play from a CD image, so start it with
`--bin <SKIPPER_1.CUE>` (as in `skipper.exe --bin SKIPPER_1.CUE --songs`); the green arrow starts the game, the red
button quits.

### Controls
| | Mouse / keyboard | Controller (DualSense, DualShock 4, Xbox, Switch, ...) | Touch (Android, Switch) |
|---|---|---|---|
| Point | mouse | left or right stick (d-pad: slow and precise) | |
| Click, drag | left button | A / Cross (or the touchpad click); hold = drag | tap, drag |
| Load / save game | F5 / F6 | X / Square, Y / Triangle | *Load*, *Save* buttons |
| Help | F1 | Back / Share | *Help* |
| Subtitles on/off | F2 | LB / L1 | *Text* |
| Background music on/off | F3 | RB / R1 | *Music* |
| Quieter / louder | arrow down / up | LT / L2, RT / R2 | *Quieter*, *Louder* |
| Settings (language on the Nordic CD) | F7 | | |
| Close dialog, quit | Esc | B / Circle or Start / Options | *Quit*, back button |
| Fullscreen | Alt+Enter, F11 (Linux) | | |

Controllers work over USB and Bluetooth without extra software (on Windows: DualSense and DualShock 4 directly, the
rest through XInput; on Linux, Android and the Switch through SDL). The pointer follows the stick; on Android and the
Switch the game draws it itself. On a phone the buttons sit in the black bars next to the picture, on a 4:3 tablet behind the *Menu* button.
They are labelled in the game's text language (on the Dutch CD *Laden*, *Opslaan*, *Uitleg*, ...; the table gives
their meaning).

### Texture packs
Every image in the game can be replaced by a PNG of any size, for example an HD version. Start the game with
`--dumptex` and play: every image the game shows goes into
`<save folder>\mods\dump\<movie>\<name>_<w>x<h>_<hash>.png`. Edit or upscale a file, keep the ending `_<hash>.png`
(anything before it may change) and put it in `mods\textures\` (subfolders allowed) in the save folder or next to
the program. If an image is larger (2x, 3x, 4x), the player draws the whole picture at that resolution (`--hd N`
picks one yourself). Transparency comes from the original image, so a pack only has to supply colours; pixels with
alpha < 128 are transparent too. Save folder: `%APPDATA%\SkipperRE`, `~/.local/share/SkipperRE`, on Android
`Android/data/io.github.jjmhalew.skipperre/files`, on the Switch `/switch/skipperre`.

## Building
- **Windows** (10 or 11, nothing to install first): double-click `build.bat` → `out\skipper.exe`. The C compiler is
  [Zig](https://ziglang.org): a `zig` on PATH or `pip install ziglang` is used when present, otherwise `build.bat`
  downloads the official Zig 0.16.0 for Windows (about 95 MB) once into `tools\zig` and checks its SHA-256.
  `build.bat dev` builds the development version `out\dev.exe` (log in the console, UBSan). In Git Bash `./build.sh`
  works too (see below).
- **One exe for yourself**: double-click `make_standalone.bat` → `SkipperRE-standalone.exe` (about 165 MB), with
  `skipper.exe` and **your** game files inside: runs without CD, image or `data` folder. The game files come from
  `extract\`, `data\` or `%APPDATA%\SkipperRE\data` (an image unpacked earlier), or from the folder you give it, such
  as the CD drive: `make_standalone.bat D:\`. It contains the game itself, so it is for your own use only: never share
  or upload it.
- **Linux**: `./build.sh` → `out/skipper` (Debian / Ubuntu: `sudo apt install build-essential libsdl2-dev`).
- **Android**: `cd android` and `./gradlew assembleRelease` (or `assembleDebug`) → `android/app/build/outputs/apk/`.
  Needs the Android SDK with NDK 27.2 and CMake 3.22 (Android Studio can open the `android` folder too). The build
  fetches SDL 2.32.10 and checks its SHA-256. Without your own key (`-PskipperKeystore=... -PskipperKeyAlias=...
  -PskipperKeyPassword=...`) the release APK gets the debug key; fine for installing yourself.
- **Nintendo Switch**: `./build_switch.sh` → `skipperre.nro`, with devkitPro's devkitA64, libnx and switch-sdl2.
  Without `$DEVKITPRO` set it builds in the official `devkitpro/devkita64` Docker image instead. The same SDL code as on
  Linux and Android; the Switch parts are in `src/switch.c` (first start, messages) and a few `__SWITCH__` lines in
  `src/host_sdl.c` and `src/plat_posix.c`.
- **CI**: `.github/workflows/build.yml` builds all four on every push; a tag `v1.2.3` (equal to the version in
  `res/skipperre.rc`) makes a GitHub release with the zip, the tar.gz, the APK and the Switch zip. For a fixed Android key: the
  secrets `ANDROID_KEYSTORE_B64`, `ANDROID_KEY_ALIAS` and `ANDROID_KEY_PASSWORD`.

## License
The code in this repository is under the [GNU General Public License v3.0](LICENSE) or later. That does not cover the
original game, the game files or the trademarks; those belong to their owners and are not part of this project.
`src/stb/` contains stb_image, stb_image_write and stb_truetype by Sean Barrett (public domain or MIT, see the end of
each file); `res/fonts/` the Liberation fonts (SIL Open Font License, `res/fonts/LICENSE`);
`android/app/src/main/java/org/libsdl/` is SDL's Android code (SDL 2.32.10, zlib license). `src/pad.c` and the
Android and Switch setup come from [WoodyRE](https://github.com/jjmhalew/WoodyRE).

# Development and reverse engineering
The game was made with **Macromedia Director 5** (file version 1217). So there is no game code in an exe: all logic
is Lingo bytecode in the `.dxr` movies. The port is therefore its own Director 5 runtime (RIFX container, cast,
score, Lingo VM) plus native replacements for the few DLLs the game calls.

The start movie `start.dxr` is not a separate file on the CD: it is inside the projector `start32.exe`, on the Dutch
CD as a deflate stream in the Wise installer `SETUP.EXE`, on the Nordic CD as a plain `START32.EXE`. The player pulls
it out once (`src/disc.c`: its own inflate and ISO9660 reader). For the Python tools you can still fill `extract/` and
`game/` by hand (`.gitignore`).

## Status
- **Disc taken apart**: Enhanced CD, session 1 = 15 audio tracks (music), session 2 = ISO9660 data track
  (LBA 99406). `tools/iso.py` gets the files out of `SKIPPER_1.BIN` → [docs/ANALYSE.md](docs/ANALYSE.md)
- **RIFX container** (`tools/rifx.py`): imap/mmap/KEY*/CAS*/CASt, little-endian XFIR
- **Lingo disassembler** (`tools/lingodis.py`): all 596 scripts / 1560 handlers, 0 unknown opcodes;
  `--stats` gives the use of builtins (82 of them), properties and entities
- **Bitmaps + palettes** (`tools/dcast.py`): 1/8 bpp, RLE, CLUT members → PNG
- **Decompiler** (`tools/lingodec.py`): readable Lingo of all movies
- **Score, labels, cast libs** (`tools/score.py`), frame rendering with inks 0/8/36
- **Start movie from the projector** (`tools/projector.py`)
- **Native engine in C** (`src/`): its own Director 5 runtime that plays the original movies. See
  [TODO.md](TODO.md) for what is done and what still needs checking.

## Native engine
| File | What |
|---|---|
| `src/dfile.c` | RIFX container, internal and external casts (per owner id), bitmaps, `snd `, STXT, scripts, score |
| `src/lingo.c` | values (refcounted), symbols, globals, the bytecode VM, objects with ancestor chain |
| `src/builtins.c` | lists, property lists, strings, arithmetic, types |
| `src/player.c` | movies, frames, events (sprite → cast member → frame → movie), puppets, sprite/member properties, `go`, palettes, transitions, MIAW windows |
| `src/stage.c` | compositing to 32-bit with inks (copy, matte, bg transparent, blend, ...), shapes, film loops; at scale 1-4 (texture packs) |
| `src/text_gdi.c`, `src/text_ttf.c` | text members: GDI on Windows, stb_truetype + Liberation Sans/Mono elsewhere |
| `src/texpack.c` | texture packs: `--dumptex`, `mods/textures`, HD replacements |
| `src/xobj.c` | INI, FileIO, MovUtils, DLLGlue → MMSYS.DLL (CD audio, LoadSaveGame), KEYBOARD.DLL, USER.EXE |
| `src/ini.c` | INI files like Windows' GetPrivateProfileString/WritePrivateProfileString (on every platform) |
| `src/video.c` | digital video: AVI parser and its own Cinepak decoder (ABC game), audio track through the mixer |
| `src/pack.c` | game files appended to the exe (one exe, see below); real files take precedence |
| `src/disc.c` | finding the game files, unpacking BIN/CUE/ISO/CloneCD IMG (also a BIN without CUE), `start.dxr` from `START32.EXE` or `SETUP.EXE` |
| `src/sound.c` | mixer (waveOut on Windows, SDL elsewhere), CD audio straight from `SKIPPER_1.BIN` through the `.CUE` |
| `src/main.c` | start, main loop, input queue, transitions, intro skip, language of the Nordic CD, headless test mode (virtual clock) |
| `src/host_win.c`, `src/host_sdl.c` | window, input, cursors, messages, file picker, printing: Win32 / SDL2 |
| `src/plat_win.c`, `src/plat_posix.c` | files, folders, time, mutex (`src/plat.h`); outside Windows case-insensitive paths |
| `src/pad.c`, `src/pad_sdl.c`, `src/padinput.c` | controllers (raw HID / XInput on Windows, SDL elsewhere) as mouse and hotkeys |
| `src/android.c` | Android: first start (file picker), messages |
| `src/switch.c` | Nintendo Switch: first start (unpacking the CD image from the SD card), messages, slot picker |
| `src/touch.c` | touch screen (Android, Switch handheld): the first finger is the mouse, buttons in the black bars |
| `src/dbgheap.c` | debug heap (`DEFS=-DDBGHEAP`): canaries + quarantine, reports file:line |

Semantics the game turned out to need (and the port had to copy):
- `birth(script "X")` / `new` is always the constructor, even when the calling script has its own `birth`.
- A script or object as the first argument receives the call (`Event(script "LocScriptC5", ...)`),
  also for a local call (`mouseUp(script "HSC5ABC")` from a script with its own `mouseUp`).
- `objcallv4` on a name: the first argument (a symbol) is the name of a variable (local/arg/property).
- Hotspots are shapes with ink 36 and fore = back: invisible but clickable.
- Film loop sprites with castLib -1 refer to the cast of the loop itself.
- `objcallv4` on a name falls back to an existing global, even when the handler doesn't declare it:
  `add(gAnimNotify, ...)` (all AnimEnd events) and `symbolp(gEffectNotify)` (exits closed during a sound effect)
  only work that way.
- The sprite script comes from the current frame for puppet sprites too (Magnus frame 3 puts it on channel 10 by
  mistake; in frame 4 it is on 11, the mailbox).
- `moveableSprite`: Director drags the sprite itself; on release `mouseUp` sees the new position.
- Clicks: only matte and mask ink test per pixel; with background transparent the whole rectangle counts.
- Editable fields (textFlags bit 0): keys go to the field's sprite script first; only what isn't caught (or does
  `pass`) goes into the field. `set the textFont of field` is get/set type 11.
- Digital video: registration point in the centre; `movieTime`/`duration` in ticks.
- String literals in the scripts are Mac Roman (compiled on a Mac), the texts in the casts Windows-1252; the port
  converts the script literals at load, as the Windows projector did (`src/dfile.c`).

```bash
./build.sh                                   # out/skipper.exe (debug build with UBSan + PDB)
RELEASE=1 ./build.sh                         # optimised, without UBSan (for playing)
OPT=-O0 DEFS=-DDBGHEAP ./build.sh            # with the debug heap
./out/skipper.exe extract                    # play (window as large as fits; Alt+Enter = fullscreen)
./out/skipper.exe extract --fullscreen       # fullscreen right away; --scale N for a fixed window size
# headless testing: run N frames, click/drag, periodic screenshots, dump globals/channels
./out/skipper.exe extract --click 320 240 170 --click 400 100 300 --shot 700 out/run --every 50 --dump
./out/skipper.exe extract --click 320 240 170 --click 320 393 300 --drag 100 350 150 450 420 --shot 520 out/coin --dump
python tools/filmstrip.py out/run out/strip.png 6
sh tools/regress.sh                          # regression test: 11 minigames + mailbox/coin/boss/save/bucket
DATA=releases/no/files sh tools/regress.sh   # the same on the Nordic CD (folder with the unpacked CD)
```
Storage (INIs, saved games): `%APPDATA%\SkipperRE`.

### One exe
```bat
make_standalone.bat [folder]                 :: = build.bat standalone -> SkipperRE-standalone.exe (~165 MB)
out\pack.exe [exe] [datafolder] [out.exe]    :: packing only (tools\pack.c; build.bat standalone builds it)
```
`tools\pack.c` appends the movies, casts, videos and `start.dxr` (raw deflate, 195 → 162 MB) to the exe; the engine
sees them as files in the exe's folder (`src/pack.c`). For your own use only: the game files are copyrighted.

### Your own spelling books (spelling game, MMB10)
The original `STAVEDIT.EXE` is 16-bit and doesn't run on Windows 11, but the format is simple. Put a text file
`GBBOOK1.MMB` (up to `GBBOOK4.MMB`, the three small books on the shelf) in `%APPDATA%\SkipperRE`:
```
E
kat,kat.wav
hond,hond.wav
```
Line 1: `H` = hard, anything else = easy. Then one `word,sound.wav` per line (at most 200 words, 20 letters). The WAVs
(PCM 8/16-bit) go in `%APPDATA%\SkipperRE\WAV\`.

## Tools
```bash
pip install pillow
python tools/iso.py extract                              # (the path to SKIPPER_1.BIN is in the script)
python tools/discx.py <image> <folder>                   # unpack any image (ISO, BIN, CloneCD IMG; Joliet)
python tools/reldiff.py extract releases/no/files        # compare two CDs file by file
python tools/rifx.py extract/Magnus.dxr                  # chunks + cast overview
python tools/lingodis.py --out out/lingo extract/*.dxr extract/*.cxt
python tools/lingodis.py --stats extract/*.dxr extract/*.cxt
python tools/dcast.py extract/Mmb04.dxr out/cast/Mmb04   # bitmaps → PNG, text → TXT
```
