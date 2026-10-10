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

## Audit 2026-10-05 (VM, builtins and file formats against Director 5)

Fixed:
- [x] A click's or key's `go` was undone by the frame's `on exitFrame go(the frame)`: exitFrame is now skipped when a
      `go` is already pending (as in Director/ScummVM). The reward dialog (mmdlg5) could not be closed; dialog closes,
      intro/credits skips and `EndDialog` returns could get lost.
- [x] `the last char/word/item/line in x` returned VOID (MMB10 `ScrambleWord` lost a letter).
- [x] `the number of castMembers of castLib "Pictures"` returned 0 (MMB11 could not browse the pictures).
- [x] Mask ink (9) uses the next member as a 1-bit mask aligned on the regPoints, and `the clickLoc` is where the
      click started: the flashlight in room I6 shows its beam and can be dragged (verified headless with a crafted save).
- [x] `the result` = return value of the last handler (mmdlg5 offered hints for items already in the pocket).
- [x] `the volume of sound n` (per channel, 0-255): intro ducking, background music under speech, muted in the tutorial.

Second round (also fixed):
- [x] **Credits**: the richtext `Credits` member in Magnus1 is drawn from its RTE2 image (alpha runs + colour codes, as
      ScummVM decodes it) over the member's background; `the text` gives RTE1. Verified headless.
- [x] **End of Magnus1**: running off the end of the stage's score quits, as a projector does (logos at tempo 2, then
      StopGame); `halt` quits. A go past the end and dialog windows still stop on the last frame.
- [x] A sound the score started stops when its sound cell ends (Magnus1 `DrumLoop2`); puppetSound/playFile take over.
- [x] Built-in palettes: -101 = Windows system, -102 = Windows D5 (tables from ScummVM); the movie default palette (VWCF)
      counts from 0, so -100 there is -101. Magnus1 logos and the Mmtutor end screen.
- [x] MIAW: `the mouseH/V`, clickLoc, rollOver and mouseMember are window-local (Mmdlg1 slider track click verified);
      windows get `idle`; window sprite cursors are shown from that window's cast.
- [x] Fmap entries at offset 36 with the id at +6 (Courier New in Mmb02); unmapped font ids stay Arial. Field text drop
      shadow (spec byte 24): `Points`, save-slot numbers.
- [x] `puppetTransition` time 0 = fastest (100 ms); "changing area only" (score spec[3] bit 0 clear, 4th argument of
      puppetTransition) runs the effect in the rectangle that changes.
- [x] `sound fadeOut ch, ticks` fades in the mixer; film loops step once per score frame and per sprite.
- [x] `hilite chunk of field` pops castLib + field + 8 (selection itself not drawn), `delete item/line/word` removes the
      delimiter/spaces, `put into item/line N` pads, `the doubleClick` works. (`send()` was never missing: it is a movie
      handler in Magnus.)
- [x] `tools/lingodec.py`: `exit` inside handlers, f(variable) for D4-style calls, hilite operands.

Third round (score/drawing, input/fields, sound/XObjects; fixed):
- [x] Palette transitions in real time with D5's speed table (ScummVM `kFadeColorFramesD5`): `puppetPalette x, 60`
      (every room change, the dialogs) is instant instead of a 2-frame blend over the new room; the intro fades in from
      black (palette cell speed 28, about half a second, the frame waits for it).
- [x] A frame without a palette cell uses the last palette cell before it in the score, also after a jump (Intro's
      click to "IntroEnd" during one of the white flash frames).
- [x] Shift, Ctrl, CapsLock etc. on their own send no keyDown/keyUp (MMB09 counted a lone Shift as a wrong letter);
      numpad Enter is keyCode 36 on Linux/Android too (MMB02 answers).
- [x] Mixer interpolates between samples (11/22 kHz sounds at 44.1 kHz without the harsh stair steps).
- [x] INI XObject `mGetPrivateProfileString` honours its Size argument; DLLGlue `VkKeyScan` returns 65535 for "no key".
- [x] Text layout as in Director's TextEdit (`src/textlay.h`, shared by GDI and TTF): a word wider than the field
      breaks at a letter, and an editable field scrolls so the insertion point stays visible. A long name in the save
      dialog (`SaveText`, about 10 letters wide) shows its end while typing; the slot label wraps instead of losing its
      start. All other text renders pixel-identical (regression + intro/tutorial/room/options shots).
- Not used by the game, left as they are: tempo waits/delays, trails, inks other than 0/8/9/32/36, shape types and
  patterns (all shapes are invisible hotspots), score blend (only in an unreachable frame), fade to black/white and
  colour cycling, timeouts, `selStart/selEnd`, markers, `play done`, ADPCM WAVs.

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
- [x] **All 12 minigames** actually playable: rounds, scoring, back to the room. *(2026-10-05: real rounds won
      headless in all 11 books plus the reward mirror: answers computed from the scripts, points awarded, wrong answers
      tried, back to C5 with the points kept. Found and fixed: the spelling game (MMB10) could not place letters,
      `the clickOn` ignored moveable sprites; now a regression check. In Calc, Clock and Memory the score in the
      corner is small and black: the `Points` field there was saved like that, probably the same in the original.
      `tools/fuzz.py` only used 64 events per run until the same day, see the harness fix.)*
- [x] **Whole adventure**: 47 locations, all puzzles (`JobIsDone` requires 18 conditions: mirror,
      bucket, pacifier, nut, letter, dice, food, nose, trousers, crown, broom, hat, flowers, …).
      Mainly check the animation chains (`AnimEnd` via `gAnimNotify`, only fixed on 2026-09-25).
      *(2026-10-05: verified headless from crafted saves: 60 puzzle tests incl. every AnimEnd chain, an exit sweep of all
      47 locations, and the last condition starting Magnus1. No engine bug. One trap is in the original scripts: opening
      a book while Skipper pillow-fights or sleeps in C5 leaves `gAnimUseCount` at 1, so C5 can't be left until a game
      is loaded (F5); the port keeps that as it is.)*
- [ ] Does D5 send `stepFrame` to the actorList on `updateStage` too, or only when entering a frame (the port)? Only
      changes animation timing.
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
- [x] Palette fades between locations (`puppetPalette … 60`): instant, as speed 60 is in D5 (third audit round).

## P3 — host / distribution

- [x] **Fullscreen** (Alt+Enter, `--fullscreen`), resizable window with 4:3 letterbox, per-monitor DPI awareness,
      default window size = largest scale that fits. *(not seen in a window yet: check!)*
- [x] Game files found automatically: data folder, `%APPDATA%\SkipperRE\data`, CD drive, or a BIN/CUE/ISO that is
      extracted once (`src/disc.c`); `start.dxr` is pulled from the Wise installer. *(MDF/MDS not supported;
      `tools/iso.py` still has a hard-coded path)*
- [x] Release build (`RELEASE=1 ./build.sh`: -O2, no UBSan, GUI subsystem = no console window), window icon from
      `Magnus.ico` on the disc (or `res/skipperre.ico`), version info from `res/skipperre.rc` (1.0.0).
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
- [x] **Nintendo Switch** homebrew (`build_switch.sh` → `skipperre.nro`, `src/switch.c`): CD image on the SD card
      unpacked at the first start, Joy-Cons as controller, touch in handheld mode. Verified in the Eden emulator
      (unpack, restart, play). *(not on real hardware yet: speed and memory unknown)*
- [x] **Controllers** (DualSense / DS4 / XInput on Windows, SDL elsewhere) as mouse + function keys; SDL path
      tested with a virtual controller. *(a real pad not tried yet)*
- [x] **Texture packs** (`--dumptex`, `mods/textures`, HD 2-4x) and **intro skip** when a save exists.
- [x] **CI** (`.github/workflows/build.yml`): Windows zip, Linux tar.gz, APK, Switch zip; release on a `v*` tag.
- [x] Optional: a "Liedjes" menu for the 15 CD-audio songs: `--songs` (`src/songs.c`): `Liedjes.bmp` with the
      hotspots, hover sounds and actions from `Liedjes.ini`, object n = audio track n+1 (Enhanced CD); the arrow starts
      the game, the red button quits. Verified headless (track 1 and 14, back arrow); not on Android (no command line).

## Other releases (docs/RELEASES.md)

- [x] **Nordic CD** (*Magnus & Myggen*, Danish / Norwegian / Swedish / Finnish, Director 5): `start.dxr` from
      `START32.EXE` (cached as `start_nordic.dxr`), `MAGNUS.INI` [Language] from the system language or `--lang`,
      CloneCD `.img`/`.ccd`, window title and Android buttons in the game's text language, port messages in English
      unless the system is Dutch. Regression 16/16 on Windows and Linux; extraction from the archive.org image tested;
      single exe tested; Android emulator: Swedish buttons, switching the text language in the F7 screen relabels them.
      *(nobody has played it through)*
- [x] **Lingo string literals are Mac Roman** (both CDs): converted to Windows-1252 at load (`src/dfile.c`). Fixes the
      Nordic dialog texts and the ABC letters; for the Dutch CD it changes the case table of MMB10 (check typing
      é/ë in the spelling game).
- [x] Dutch 2003 re-release: same game files as 1997, works unchanged.
- [x] Danish 1996 first release (Director 4, `MAGNUS0/1.DXR`, main movie inside `MAGNUS.EXE`): Director 4 cast,
      score, Lingo and projector support, MMSYS/MMPRINT calls (docs/RELEASES.md section 4). Regression 17/17 on Windows
      and Linux, fuzzed, ISO install tested. *(nobody has played it through)*
- [ ] *Superstarter* re-releases (Danish 2006 Director 8.5, Swedish Director 10; InstallShield cabs): need cab
      extraction and the Director 8.5/10 formats. Low priority for the same reason. Looked at on 2026-10-05: plain RIFX
      (no Afterburner) with `DRCF` (version 0x73a), `LctX`, sounds as `snd ` + `sndH` + `sndS`, the D6+ score layout;
      Lscr handler records are 46 bytes (lingodis reads them now; the literal values still come out wrong), and the
      scripts were rewritten for Xtras: Buddy API (`baReadIni`/`baWriteIni` instead of the INI XObject), FileIO and
      PrintOMatic Xtras. So: unshield in C, D8 score/sound/literals and those Xtras: a project of its own, for content
      that the Nordic CD already plays.
- [x] Android app name per system language: *Magnus og Myggen* (da), *Magnus & Myggen* (nb/no), *Magnus och Myggan* (sv),
      *Manu ja Matti* (fi), otherwise *Skipper & Skeeto* (checked with `aapt dump badging`).

## Out of scope

- The catalog/demos (`Catalog/`, `Transpos.exe`, `Launcher.exe`, HTML order forms), `SETUP.EXE`, `VFW/`
  (Video for Windows 1.1 installer).
- The known "member niet gevonden" warnings (`AAF1A1`, `OBB1A1`, `MMH1A1`, …): typos in the original
  animation lists, harmless.
