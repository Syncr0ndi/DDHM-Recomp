# Roadmap: source port of Dangerous Dave in the Haunted Mansion

Target: C++20 + SDL3, CMake, Windows 11 (x64). The user's own game files (.DD2 and DAVE.EXE) are loaded at run time
and never redistributed with the port. Scope: the Softdisk release, DAVE.EXE 1.12 (the "Rooms of Doom" build,
ROOMS.EXE, is out of scope).

| Stage | Goal | Status |
|---|---|---|
| 0 | Extract archive, identify packer and compiler | done |
| 1 | LZEXE unpacker (`tools/unlzexe.c`, in-memory `dd2::lzexe_unpack`) | done, verified |
| 2 | Tiles, levels, PIC screens, HUFF, sprites, INTRO.DD2 (RLEB) | done, regression-tested |
| 3 | Data inside DAVE.EXE: tile8, font, pictures, sounds, strings, palettes, all data tables (read at run time) | done, tested (test_flow, test_assets) |
| 4 | Entities, tile info, collision, player, enemies, bosses, doors, scoring | done, tested, user play-tested |
| 5 | Program flow: title, level intro, death animations, game over, high score, continue, ending | done, tested (test_flow, test_screens) |
| 6 | HUD (ammo meter, TAB window), F1-F5 / ESC windows, F3 key set-up, F4 gamepad buttons, debug keys | done |
| 7 | PC-speaker sound (24 effects, priorities, F2), optional soft mode | done, tested |
| 8 | SDL3 front end: 70 Hz, 4:3 / square pixels, fullscreen, gamepad, settings + high score file | done |
| 9 | Packaging: static exes, original icon, DDHM Launcher (Game -> Assets), CI build, release workflow (zip, no game data), copyright check test | done (release not yet published) |

## Verification method
No emulator is used, also not for checking. Every routine is ported from the disassembly and checked by:
deterministic unit tests with hand-derived expectations (test_player, test_enemy), headless runs of the whole program
with timing and sound checks against the code (test_flow), screen CRC regression of scripted runs (test_screens), PNG /
WAV renders for eyeballing (ddhm_run), and the user's play-testing.

## Left open by choice
* Developer screens: F12+T sprite test, F12+M memory usage, `/LEVEL` TEDLEVEL editor hook.
* The uncalled 1991 intro routine (0x6A28) is not shown, as in the original.
* Rooms of Doom (ROOMS.EXE / NEWINTRO.EXE).

## Build and CI
`.github/workflows/build.yml` compiles the Windows x64 build with MSVC on every push and runs the data-free tests;
`release.yml` publishes `ddhm-<tag>-windows-x64.zip` for tags `v*`. Local builds use LLVM-MinGW clang + Ninja (README).
Keep the repository private and never commit game data (`.gitignore` excludes it).

## Legal
The game and its art are copyrighted by their owners. This project contains only original tooling and
documentation. Use your own legally obtained copy of the data files, and do not redistribute them.
