# Third-party notices

## This project

All code in this repository is © 2026 Syncr0ndi and released under the [MIT licence](LICENSE).
Every source file carries an `SPDX-License-Identifier: MIT` header. That includes:
- the LZEXE 0.91 unpacker (`tools/unlzexe.c`, `dd2::lzexe_unpack`), written for this project from the public format
  description;
- the program icon and the logo (`tools/make_icon.cpp`, `docs/images/logo.svg`), original designs made for this
  project. They are not artwork from the game.

## The game

*Dangerous Dave in the Haunted Mansion* &copy; 1991–93 Softdisk Publishing. **No part of the game is in this
repository or in the release download:** no program code, graphics, sound, text or data tables. Players provide their
own copy of the MS-DOS game in the `Game` folder; on first launch the launcher copies the files the port needs into
`Assets` on the player's PC, and the program reads them from there. `tests/test_no_game_content.cpp` checks that the
released executables contain no block of bytes and no text of the game files. The MIT licence covers only this
project's own code, not the game.

This is an unofficial, non-commercial fan project, not affiliated with or endorsed by the copyright holders. Product
names are used only to identify the game this project works with.

## Libraries used by the release

| Component | Use | Licence | How it's included |
|---|---|---|---|
| [SDL3](https://www.libsdl.org/) 3.2.0 | Window, input, audio | zlib | Downloaded by CMake at build time and linked statically. Licence text shipped as `LICENSES/SDL3-LICENSE.txt` in the release. |
| Microsoft Visual C++ runtime (static) | C/C++ runtime | Microsoft Visual Studio licence terms (redistributable) | Linked by the compiler on GitHub's Windows runner. |

The release uses the Windows Universal C Runtime (part of Windows 10/11) and the standard Windows system libraries,
so no other DLLs are shipped.
