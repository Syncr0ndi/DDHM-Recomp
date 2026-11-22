# Development guide

## Requirements
Windows 10/11, CMake 3.24+, and either LLVM-MinGW (clang) + Ninja or Visual Studio 2022. SDL3 is downloaded by CMake on
the first configure. Tests and tools need your own copy of the MS-DOS game (a `Game` folder, or an `Assets` folder
written by the launcher).

## Build
```
cmake -S . -B build/rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DDDHM_GAME_DIR=C:/path/to/Game
cmake --build build/rel
ctest --test-dir build/rel
```
Visual Studio: `cmake --preset win-vs2022 -DDDHM_GAME_DIR=C:/path/to/Game`, then `cmake --build build/vs2022 --config Release`.

To play a development build, run `build\rel\ddhm.exe C:\path\to\Game` (a Game or Assets folder directly), or copy
`ddhm.exe` and `DDHM Launcher.exe` next to a `Game` folder like the release.

## Layout
| Path | What |
|---|---|
| `src/main.cpp` | the game (`ddhm.exe`): SDL3 window, audio, keyboard, gamepad, settings |
| `src/launcher_main.cpp` | `DDHM Launcher.exe` (Win32): checks Game, extracts Assets, options, starts the game |
| `src/game/flow.*` | the program around the play loop: title, intro, death, game over, high score, continue, ending, F-keys |
| `src/game/machine.*`, `screen.*`, `speaker.*` | PC model: key table, 140 Hz timer, PC-speaker player, EGA page, window/text primitives |
| `src/game/object`, `player`, `enemy`, `world` | the game logic (objects, Dave, enemies, bosses, doors, score) |
| `src/dd2/` | .DD2 formats, DAVE.EXE data (tile8, font, pictures, sounds, strings, tables), Game -> Assets extraction |
| `src/viewer_main.cpp` | `ddhm_viewer`: level, collision and entity viewer |
| `tools/` | `ddhm_run` (whole game headless), `dd2tool` (PNG export), `unlzexe`, trace tools, `make_icon` |
| `tests/` | see below |
| `docs/` | reverse-engineering notes: FORMATS, ENTITIES, TILEINFO, PLAYER, ENEMIES, DOORS, FLOW, FUNCTIONS, ROADMAP |

## Tests
| Test | Checks |
|---|---|
| `test_assets` | every decoded format invariant; Game -> Assets extraction and tamper detection |
| `test_player` | Dave's physics against values derived by hand from the disassembly |
| `test_enemy` | every enemy, kills, scoring, all 25 real doors, both boss fights |
| `test_flow` | the DAVE.EXE data, text/window primitives, timer and speaker, and the whole program headless (timing, sounds, deaths, continue, god mode, TAB) |
| `test_screens` | screen CRCs of scripted runs (`tests/screens.golden`; regenerate with `--update` after an intended change) |
| `test_no_game_content` | no 32-byte block and no string of the game files is inside `ddhm.exe` or `DDHM Launcher.exe` |

All tests need the game files, so they run locally, not in CI.

## Headless runs
```
ddhm_run C:\path\to\Game --level 2 --frames 900 --key 320:+4d --key 400:=1d --png 450:shot.png --wav sound.wav
```
Keys are PC scan codes in hex (`+` press, `-` release, `=` tap); `--complete F` finishes the level at frame F;
`--god` starts in god mode. The run prints every level / state / lives change.

## Releases
`.github/workflows/build.yml` builds on every push. Pushing a tag `v*` runs `release.yml`, which zips `ddhm.exe`,
`DDHM Launcher.exe`, `README.txt` (docs/PLAYING.md), the licences and an empty `Game` folder, and refuses any other file.
Run `test_no_game_content` on the release executables before tagging.
