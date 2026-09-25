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

- [ ] **F5/F6/F7 hotkeys do nothing.** `GlobalKeyDown` uses Mac keyCodes 96 (F5 = load), 97 (F6 = save)
      and 98 (F7 = options, `mmdlg1`), but `mac_keycode()` in `src/main.c` only maps F1–F4. Add
      F5=96, F6=97, F7=98, F8=100, F9=101, F10=109, F11=103, F12=111.
- [ ] **Primary event handler outlives its movie.** In the minigames without their own
      `mouseDownScript` (MMB01/02/03/04/06/08/09/11) the Magnus handlers `MainMouseDown`/`MainMouseUp`
      stay set and are called in a movie that doesn't have them ("handler niet gevonden"). Harmless now
      (warning only). Find out whether D5 resets these on a movie switch or silently ignores a missing
      handler, and copy that.

## P1 — missing features

- [ ] **Digital video (ABC game, MMB09).** Every letter plays a clip: 26 linked AVIs in `extract/Video/`
      (66 MB), **Cinepak** 320×240 at 15 fps with 8-bit mono PCM at 11/22 kHz. The game uses
      `set the member of sprite 9`, `movieRate`, `movieTime`, `the duration of member` and the
      `gVideoPlaying`/`EndVideo` flow. Needed:
  - [ ] parse linked `digitalVideo` cast members (type 10) and their file names
  - [ ] AVI (RIFF) parser plus our own **Cinepak decoder**, to stay independent of Windows codecs
  - [ ] sprite rendering of the current frame; `movieRate`/`movieTime`/`duration`; the audio track
        through the mixer
- [ ] **`sound playFile` (external WAV).** Used by the spelling game (MMB10) for custom word lists:
      `gMMPath & "WAV\" & name`. Short WAV reader into the mixer.
- [ ] **Custom spelling books.** MMB10 reads word lists through FileIO. They are made with
      `STAVEDIT.EXE`, a **16-bit** Windows program that does not run on 64-bit Windows 11. Options:
      document the file format and write a small editor, or leave the feature out. Decide after the
      format is known.
- [ ] **Printing in the paint game (MMB11).** `PrintOMatic_Lite` is a no-op, so the print button does
      nothing. Proposal: save the drawing as PNG/BMP (plus the Windows print dialog if wanted).
- [ ] `the searchCurrentFolder` (movie prop, set once in Magnus): accept it and ignore it.

## Verification — content not played through yet

Headless it only goes as far as "opens and responds to clicks". Each item needs a real playthrough
(user in a window) or a scripted headless run:

- [ ] **Save/load**: F6 → `mmdlg3` (type a name, 16 slots), F5 → load, and "save before quitting"
      (`mmdlg2` → `mmdlg3 LeaveGame`). Check that `MMSAVn.MMS` / `MAGNUS.INI` in `%APPDATA%\SkipperRE`
      are written and restored correctly (all object flags through `GetFlags`/`LoadFlags`).
- [ ] **Options** (`mmdlg1`, F7), **map** (`mmdlg4`, map icon in the pocket), **reward** (`mmdlg5` at
      ≥ 50 points), **tutorial** (`mmtutor`, F1).
- [ ] **All 12 minigames** actually playable: rounds, scoring, back to the room. Only the first screen +
      clicks are tested now.
- [ ] **Whole adventure**: 47 locations, all puzzles (`JobIsDone` requires 18 conditions: mirror,
      bucket, pacifier, nut, letter, dice, food, nose, trousers, crown, broom, hat, flowers, …).
      Mainly check the animation chains (`AnimEnd` via `gAnimNotify`, only fixed on 2026-09-25).
- [ ] **Ending**: `CheckDone` → `go(1, "magnus1")` (`Magnus1.dxr`, never started yet; uses
      transition cast members).
- [ ] Other keys: Esc (quit), F2 (subtitles), F3 (music), +/- (volume via `CheckAdjustSound`).
- [ ] Missing member `DragSndCursor`/`DragSndCursorMask` in the music game (MMB05): check whether that
      is a bug in the original (like the other "member niet gevonden" typos) or a lookup issue in the
      engine.

## P2 — fidelity (needs someone watching/listening in a window)

- [ ] **Transitions**: types 1–10 are implemented, all others (e.g. `puppetTransition(23)` and some
      transition members in Intro/Magnus1/Mmtutor) fall back to a generic dissolve. Chunk size and
      timing are approximate.
- [ ] Timing/tempo against the original (frame rate, `Wait`/`startTimer` loops, speech sync).
- [ ] Sound: volume levels, background music loops, cut-offs in `puppetSound`.
- [ ] Text rendering (subtitle bar, score, dialog fields) against the original fonts and sizes.
- [ ] Cursors (bitmap cursors from member lists) and their hotspots.
- [ ] Palette fades between locations (`puppetPalette … 60`).

## P3 — host / distribution

- [ ] **Fullscreen** (Alt+Enter) and free window scaling. Now only integer `--scale`, no DPI awareness.
- [ ] Load data straight from the disc image (BIN/CUE, MDF or a real CD drive) instead of a prepared
      `extract/` folder. `tools/iso.py` has a hard-coded path.
- [ ] Release build without UBSan (`OPT=-O2`, no `-g`), icon (`Magnus.ico`), version info.
- [ ] Commit the regression sweep (13 hotspots + walk/drag/mailbox/boss scenarios) as a script, for
      example `tools/regress.sh`.
- [ ] Optional: a "Liedjes" menu for the 15 CD-audio songs (the original `Liedjes.exe` is a separate
      launcher; `src/sound.c` can already play CD audio from the BIN).

## Out of scope

- The catalog/demos (`Catalog/`, `Transpos.exe`, `Launcher.exe`, HTML order forms), `SETUP.EXE`, `VFW/`
  (Video for Windows 1.1 installer).
- The known "member niet gevonden" warnings (`AAF1A1`, `OBB1A1`, `MMH1A1`, …): typos in the original
  animation lists, harmless.
