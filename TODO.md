# TODO — SkipperRE

State as of 2026-09-25. Short version: the **engine is functionally complete** (every opcode, builtin,
`the`-entity and XObject call the game uses is implemented), and the main adventure runs. What's left is
one missing media type (**digital video**), a few host features, and a lot of **verification** of content
nobody has played through yet.

How this list was made:
- a static cross-reference of all 602 scripts / 1569 handlers against the engine (`lingodis --stats` plus
  the registered builtins/properties)
- headless runs of all 13 room hotspots / minigames with a burst of clicks each, collecting `[lingo]`
  warnings
- reading the decompiled scripts for features that are not reached yet (videos, dialogs, ending, custom
  spelling lists, printing)

Legend: **P0** = bug that breaks something the player will hit, **P1** = missing feature, **P2** =
fidelity/polish, **P3** = nice to have.

---

## P0 — bugs

- [x] **F5/F6/F7 hotkeys do nothing.** *(fixed)* `GlobalKeyDown` uses Mac keyCodes 96 (F5 = load), 97 (F6 = save)
      and 98 (F7 = options, `mmdlg1`), but `mac_keycode()` in `src/main.c` only maps F1–F4. Add
      F5=96, F6=97, F7=98, F8=100, F9=101, F10=109, F11=103, F12=111.
- [x] **Primary event handler outlives its movie.** *(no-op: a missing handler does nothing and the event
      passes on, which is the same as a reset; left as is)* In the minigames without their own
      `mouseDownScript` (MMB01/02/03/04/06/08/09/11) the Magnus handlers `MainMouseDown`/`MainMouseUp`
      stay set and are called in a movie that doesn't have them ("handler niet gevonden"). Harmless now
      (warning only). Find out whether D5 resets these on a movie switch or silently ignores a missing
      handler, and copy that.

## P1 — missing features

- [x] **Digital video (ABC game, MMB09).** *(done: `src/video.c`, own AVI parser + Cinepak decoder, audio track
      on its own mixer voice; verified headless: letter → key → picture → apple video → point)* Every letter plays a clip: 26 linked AVIs in `extract/Video/`
      (66 MB), **Cinepak** 320×240 at 15 fps with 8-bit mono PCM at 11/22 kHz. The game uses
      `set the member of sprite 9`, `movieRate`, `movieTime`, `the duration of member` and the
      `gVideoPlaying`/`EndVideo` flow. Needed:
  - [x] parse linked `digitalVideo` cast members (type 10) and their file names
  - [x] AVI (RIFF) parser plus our own **Cinepak decoder**, to stay independent of Windows codecs
  - [x] sprite rendering of the current frame; `movieRate`/`movieTime`/`duration`; the audio track
        through the mixer
- [x] **`sound playFile` (external WAV).** *(done: WAV loader in `src/sound.c`)* Used by the spelling game (MMB10) for custom word lists:
      `gMMPath & "WAV\" & name`. Short WAV reader into the mixer.
- [x] **Custom spelling books.** *(format documented in the README: `GBBOOKn.MMB` + `WAV\`; verified with a
      home-made book. No editor, a text editor is enough.)* MMB10 reads word lists through FileIO. They are made with
      `STAVEDIT.EXE`, a **16-bit** Windows program that does not run on 64-bit Windows 11. Options:
      document the file format and write a small editor, or leave the feature out. Decide after the
      format is known.
- [x] **Printing in the paint game (MMB11).** *(MMB11 is a colouring book: browse line drawings and print them,
      there is no painting on screen; with printing it is complete)* *(done: prints the colouring page (`maleN`, not the user's
      painting) through the Windows print dialog, fitted within 1-inch margins, landscape if the game asks; headless
      writes `print.bmp`)* `PrintOMatic_Lite` is a no-op, so the print button does
      nothing. Proposal: save the drawing as PNG/BMP (plus the Windows print dialog if wanted).
- [x] `the searchCurrentFolder` (movie prop, set once in Magnus): accept it and ignore it.

## Verification — content not played through yet

Headless it only goes as far as "opens and responds to clicks". Each item needs a real playthrough
(user in a window) or a scripted headless run:

- [x] **Save/load** *(verified headless: typing a name, saving at D5 with the coin, loading in a fresh
      session → D5, 3 points, coin in the pocket; the load dialog appears at startup when saves exist)*: F6 → `mmdlg3` (type a name, 16 slots), F5 → load, and "save before quitting"
      (`mmdlg2` → `mmdlg3 LeaveGame`). Check that `MMSAVn.MMS` / `MAGNUS.INI` in `%APPDATA%\SkipperRE`
      are written and restored correctly (all object flags through `GetFlags`/`LoadFlags`).
- [x] **Options** (`mmdlg1`, F7), **map** (`mmdlg4`, map icon in the pocket), **reward** (`mmdlg5` at
      ≥ 50 points), **tutorial** (`mmtutor`, F1). *(all four verified headless: options (incl. subtitle label after
      the actorList fix), map, tutorial, and the reward mirror via the mirror at ≥ 50 points)*
- [ ] **All 12 minigames** actually playable: rounds, scoring, back to the room. *(fuzzed with `tools/fuzz.py`:
      2 seeds x 3000-4000 frames of random clicks/drags/keys per game, no crashes, UBSan traps or unexpected
      Lingo errors; ABC played through headless incl. video. Real rounds still need a human.)*
- [ ] **Whole adventure**: 47 locations, all puzzles (`JobIsDone` requires 18 conditions: mirror,
      bucket, pacifier, nut, letter, dice, food, nose, trousers, crown, broom, hat, flowers, …).
      Mainly check the animation chains (`AnimEnd` via `gAnimNotify`, only fixed on 2026-09-25).
- [x] **Ending** *(verified headless with `--movie Magnus1`: all scenes, dragging the wand to the fairy, celebration,
      credits)*: `CheckDone` → `go(1, "magnus1")` (`Magnus1.dxr`, never started yet; uses
      transition cast members).
- [x] Other keys *(verified headless: Esc → quit dialog `mmdlg2`, F2 subtitles off, F3 music off, up/down arrows = volume
      with meter)*
- [x] Missing member `DragSndCursor`/`DragSndCursorMask` *(does not exist anywhere on the disc: a bug in the
      original, Director falls back to the arrow and so do we)* in the music game (MMB05): check whether that
      is a bug in the original (like the other "member niet gevonden" typos) or a lookup issue in the
      engine.

## P2 — fidelity (needs someone watching/listening in a window)

- [x] **Transitions** *(all 52 Director types in `src/trans.c`, with chunk size; transition members were decoded
      wrongly (type is spec[2], duration spec[4..5] in ms, chunk spec[1]); check with `--transtest dir`)*
- [ ] Timing/tempo against the original (frame rate, `Wait`/`startTimer` loops, speech sync).
- [ ] Sound: volume levels, background music loops, cut-offs in `puppetSound`.
- [ ] Text rendering (subtitle bar, score, dialog fields) against the original fonts and sizes.
- [ ] Cursors (bitmap cursors from member lists) and their hotspots.
- [ ] Palette fades between locations (`puppetPalette … 60`).

## P3 — host / distribution

- [x] **Fullscreen** (Alt+Enter, `--fullscreen`), resizable window with 4:3 letterbox, per-monitor DPI awareness,
      default window size = largest scale that fits. *(not seen in a window yet: check!)*
- [x] Game files found automatically: data folder, `%APPDATA%\SkipperRE\data`, CD drive, or a BIN/CUE/ISO that is
      extracted once (`src/disc.c`); `start.dxr` is pulled from the Wise installer. *(MDF/MDS not supported;
      `tools/iso.py` still has a hard-coded path)*
- [x] Release build (`RELEASE=1 ./build.sh`: -O2, no UBSan, GUI subsystem = no console window), window icon from
      `Magnus.ico` on the disc (or `res/skipperre.ico`), version info from `res/skipperre.rc` (0.9.2).
- [x] Commit the regression sweep *(`tools/regress.sh [exe]`, 16 checks, 2-3 s with the virtual clock; same pictures
      on Windows and Linux)* (13 hotspots + walk/drag/mailbox/boss/bucket scenarios).
- [x] **Single exe** (`make_standalone.bat` → `tools/pack.c` → `SkipperRE-standalone.exe`, ~165 MB): game files
      appended to the exe (`src/pack.c`), icon as a resource. Regression sweep 16/16 against the packed exe from an
      empty folder.
- [x] **build.bat**: Windows build without installing anything (Zig from PATH, pip, or downloaded once with SHA-256
      check), like WoodyRE; CI's Windows job uses it.
- [x] **File picker** when the game files are not found (Windows: open dialog; Linux: zenity/kdialog; Android: SAF).
- [x] **Linux** build (SDL2, `src/host_sdl.c`, TTF text): regression 16/16 in WSL Ubuntu; window not seen yet.
- [x] **Android** APK (`android/`): first start with the system file picker, touch buttons in the side bars;
      tested in the emulator (intro, room, load screen, clock game). *(the picker flow itself only by its parts:
      BIN through a file descriptor tested on Linux; not on a real phone yet)*
- [x] **Controllers** (DualSense / DS4 / XInput on Windows, SDL elsewhere) as mouse + function keys; SDL path
      tested with a virtual controller. *(a real pad not tried yet)*
- [x] **Texture packs** (`--dumptex`, `mods/textures`, HD 2-4x) and **intro skip** when a save exists.
- [x] **CI** (`.github/workflows/build.yml`): Windows zip, Linux tar.gz, APK; release on a `v*` tag.
- [ ] Optional: a "Liedjes" menu for the 15 CD-audio songs (the original `Liedjes.exe` is a separate
      launcher; `src/sound.c` can already play CD audio from the BIN).

## Out of scope

- The catalog/demos (`Catalog/`, `Transpos.exe`, `Launcher.exe`, HTML order forms), `SETUP.EXE`, `VFW/`
  (Video for Windows 1.1 installer).
- The known "member niet gevonden" warnings (`AAF1A1`, `OBB1A1`, `MMH1A1`, …): typos in the original
  animation lists, harmless.
