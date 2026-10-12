# Coverage

How much of the original game's Lingo the engine in `src/` can run, and what it still misses. Every handler of every
script in the Director files counts once, weighted by the length of its bytecode. Use it to find features the engine
lacks and code nobody has reached in a test run yet. Update it after engine work or a new trace.

## How it is made
```bash
python tools/coverage.py [--trace out/trace.log] --write --html
```
- Input: `extract/*.dxr` and `extract/*.cxt` (any case), or the files you name. Include `start.dxr` (from
  `tools/projector.py`): the other movies call its handlers. Without `--write` and `--html` it only prints the report:
  totals per file and class, every missing feature with the handlers that use it, and every unused handler.
- `--write` rewrites the part of this file between the generated markers. That part has the totals, the missing
  features and the number of unused handlers per file, but no handler names.
- `--html [FILE]` writes the **coverage map** (default `out/coverage.html`): one page with every handler of every file
  as a block as wide as its bytecode, coloured by class, with an "Open" table of the missing features. It holds the
  original's handler and cast member names, so it stays out of the repository.
- `--trace LOG` (repeatable) is the stderr of a traced run. With `--trace` the engine prints every handler it enters
  (`vm_call` in `src/lingo.c`):
  ```bash
  ./out/skipper.exe extract --trace 2> out/trace.log                                  # play, then quit
  ./out/skipper.exe extract --trace --click 320 240 170 --shot 700 out/run 2> out/trace.log   # headless
  ```
  On Android and the Switch stderr already goes to `skipper.log`. Several logs (one per minigame, say) can be given
  together.
- `--src DIR` reads another copy of the engine sources (default `src/`).

## What it measures
The engine's abilities are read from the C sources, not kept in a list: the `vm_register` names (builtins), the names
`vm_call` handles itself (`return`, `pass`), the `_stricmp` names of `player_the`, `player_movie_prop`,
`player_set_movie_prop`, `player_objprop`, `player_set_objprop`, `rect_idx`, `member_get` and `member_set`
(`src/player.c`), the cases of `player_get`/`player_set` and `sprite_get`/`sprite_set` (the `get`/`set` property
ids), the XObject factories, methods and MMSYS functions of `src/xobj.c`, the opcodes `vm_call` has a case for, and
the events the player sends (`sym("enterFrame")` and the like). All names compare case-insensitively, as Lingo does.

The game's side comes from `tools/lingodis.py`. A small stack simulation per handler finds the arguments of each call,
so that a property id (`the timeoutLength` = `get 7/31`), an XObject method (`gIni(mGetPrivateProfileString, ...)`) or
a DLL function (`DLLGlue(mNew, dll, "CDPlayTrack", ...)`) can be checked.

### Classes
Each handler gets the first class that applies:

- **runs**: the trace shows it was entered.
- **missing**: it uses something the engine does not implement. This can be a builtin or call that is no handler of
  any script in the given files, an XObject or Xtra (`openXLib`, `xtra`), an XObject method, an MMSYS function, a
  `the` entity, a movie property, a `get`/`set` property (sprite, member, `the` animation properties, ...), an object
  property, or an opcode without a case in `vm_call`. An event handler the engine never sends (`mouseEnter`,
  `prepareFrame`, `beginSprite`, `timeout`, the window events) is missing too, unless the game also calls it by name.
- **unused**: nothing reaches it. It is not a Director event (`mouseUp`, `exitFrame`, `startMovie`, `stepFrame`, `new`,
  ...), and its name appears nowhere in the bytecode of another handler: not as a call, an `objcallv4` variable, a
  symbol, or a word in a string literal (strings feed `do`, `value` and the `mouseDownScript`-style properties).
- **supported**: the rest. Statically, everything it uses is implemented.

### Limits
- **runs** is matched by name only: the trace prints `> name(args)` without the script or movie. A name defined in
  several scripts or files (every `mouseUp`, `exitFrame`, a minigame's `Init`) marks all of them. When the first
  argument is an object or a script (`<offspring 12>`, `(script 12)`), only the scripts with that cast member number
  count, if any of them has the handler, but an inherited handler (`ancestor`) or the same number in another file can
  still be wrong. A trace from another CD (the Nordic one, the Danish D4 one) only fits its own files. Trace names that
  match no handler are listed at the end of the report.
- **missing** is static and generous:
  - Any handler of any script in the given files counts as callable, also when it lives in another movie or in a score
    script that Director would not call by name. A call to the name of a global counts as an XObject call (the engine
    calls an XObject in a global of that name), so only its method is checked.
  - A property the engine accepts but ignores, or returns 0 for (an unknown `set` of a `the` animation property, a
    movie property other than `actorList`/`itemDelimiter`, a member property `member_get` does not know), counts as
    missing. A builtin registered as a no-op (`preLoad`, `moveToFront`, `erase`, ...) counts as implemented.
  - An object property counts as implemented when the engine knows it for rects, members or windows, or when any
    script declares it as a `property`, or when it is a symbol anywhere in the game (a property-list key). That hides
    real gaps rather than inventing them.
  - Only constant arguments are checked: a property id, method or DLL function that is computed, and the text of
    `do`/`value`, are not. The stack simulation ignores jumps; where it loses track (inside `case` and
    `repeat with ... in`), the value is unknown and nothing is flagged.
  - It does not check behaviour: a feature that is implemented but wrong counts as implemented.
- **unused** is generous too: one use anywhere is enough, even from code that is unused itself; calls to itself do
  not count. A handler reached only through a name built at run time (`do "Room" & n`) looks unused, and a handler
  named after a common word is easily marked as used.

<!-- generated by tools/coverage.py -->
Not generated yet: run `python tools/coverage.py --write` with the game's files in `extract/` (and a `--trace` log for
the "runs" class).
<!-- end of generated -->
