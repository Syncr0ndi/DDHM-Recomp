Dangerous Dave in the Haunted Mansion - native Windows port
============================================================

This is a fan-made native Windows version of the 1991-93 Softdisk game, rebuilt from the original DOS program.
It contains no part of the game: you need your own copy of the MS-DOS game (the Softdisk release, version 1.12).

Setting up
----------
1. Unzip this package anywhere. You get:
     DDHM Launcher.exe   start here
     ddhm.exe            the game (started by the launcher)
     Game\               put your copy of the MS-DOS game here
2. Extract the original game files into the Game folder: DAVE.EXE, EGATILES.DD2, LEVEL01.DD2 ... LEVEL08.DD2,
   S_DAVE.DD2, S_CHUNK1.DD2, S_CHUNK2.DD2, S_FRANK.DD2, S_MASTER.DD2, TITLE1.DD2, TITLE2.DD2, PROGPIC.DD2.
3. Start "DDHM Launcher". It checks the files and, on the first launch, extracts what the port needs into a new
   Assets folder next to it. Then press Play.

Without the original files the port does not start. If you change or replace the game files, press
"Extract assets again" in the launcher. Settings, your keys and the high score are kept in
%APPDATA%\DDHM-Recomp\DDHM (a CTLPANEL.DD2 from the DOS game in the Game folder is imported once).

Keys (as in the original; F1 shows them in the game)
------------------------------------------------------
  Arrow keys          move; UP / DOWN aim the shotgun; UP at a door opens it, UP again goes through
  CTRL                jump (DOWN + CTRL drops through a floor)
  ALT                 fire
  F1 help   F2 sound on/off   F3 change keys   F4 gamepad buttons   F5 reset game
  TAB status window   ESC quit

Extras of this version
  SPACE or Z          also jump;  X also fires (unless you gave those keys another job with F3)
  Gamepad             D-pad or left stick, A = jump, B or X = fire (F4 swaps jump and fire)
  F11 or Alt+Enter    fullscreen
  F9                  4:3 picture (default) / square pixels
  F8                  softer speaker sound
The launcher has the same fullscreen / square pixels / softer sound options.
