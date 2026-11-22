# Program flow, screens, sound and input (port of DAVE.EXE, see src/game/flow|machine|screen|speaker.*)

Addresses are image offsets of the unpacked DAVE.EXE for code and DS offsets for data. Everything here is read from the
disassembly; the checks are in `tests/test_flow.cpp` (behaviour and timing) and `tests/test_screens.cpp` (screen CRCs).

## Data that lives inside DAVE.EXE (src/dd2/exe.*)
The port unpacks the user's DAVE.EXE in memory at start-up (LZEXE 0.91) and checks it is version 1.12. Nothing taken
from it is stored in the repository.

| What | Where (image offset) | Format |
|---|---|---|
| Graphics header | 0x122B0 (segment 0x122B, read at 0x07AE) | `+0` u32 plane size (0x33B0), `+8` picture table offset, `+0x0C` sprite table offset, `+0x10` tile8 count (32), `+0x22` tile count (858), `+0x28` picture count (41), `+0x2A` u32 picture base in the planes (0x100), `+0x2E` sprite count (199) |
| Tile8 + picture planes | 0x18900 (segment 0x1890 = DS:0x9FFA..0xA000) | 4 consecutive colour planes of 0x33B0 bytes; tile8 *n* = 8 bytes at plane offset n*8 |
| Picture table | 0x122F0 | 16-byte records: u16 width (bytes), u16 height, u32 offset (+ picture base), name[8]. 0-39 = death frames (KNIFE, SLIME, ZOMBIE, SPIDER, WOLF, GHOST, SKULL, SLICE; 5 each, 48x48), 40 = THEEND (208x56) |
| Proportional font | 0x106D0 (segment 0x106D = DS:0x947A) | u16 height (10), u16 location[256], u8 width[256]; each glyph = an outline bitmap then the glyph bitmap, (w+7)/8 bytes per row. The draw routine (0x963D) uses only the second |
| Sound table | 0x257C0 (segment 0x257C = DS:0x9498) | "SND\0", u16 data size, u16 count (24); records of 16 bytes {u16 offset, u8 priority, u8, name[12]}; data = one u16 PIT divisor per 140 Hz tick, 0 = silent, 0xFFFF = end |
| Fade palettes | DS:0x29EC | 4 steps x 17 bytes (16 palette registers + border) |
| Scan code -> ASCII | DS:0x2D7A | 128 bytes (US layout) |
| Strings | DS:0x0100..0x2CE4 | read by offset at run time (help text, messages, story, key names) |

The window tiles are tile8 1..8 (stone frame: corners 1/3/6/8, top 2, sides 4/5, bottom 7) and 0x1B (white inside).
Tiles 9..12 are the red diamonds and 16..21 arrows (used by the joystick calibration).

## Screen model (src/game/screen.*)
* The page is a canvas wider than the screen (EGA line width 64 bytes in play, 80 on the title). `org` = DS:0x3172 (where
  the drawing routines start), `view` = CRTC start address + pel pan. In play the page origin is the tile-aligned camera
  and the view starts `(cam >> 4) & 15` pixels further in, so message boxes shift with the sub-tile scroll as in the
  original.
* Palette registers are kept separately; the monitor is RGBI in the 200-line modes (bit 4 = intensity, value 6 = brown).
* Text: 0x963D draws in EGA write mode 0 with the XOR function (DS:0x43A0 = 0x18) and map mask = colour (DS:0x439A, 15),
  so text is black on the white window and drawing a character twice removes it (the blinking `_` cursor of 0x8AAE).
  `\n` = 10 pixels down to the left margin; 0x7F + letter sets the colour (letter - 'A').
* 0x82BE window(x0,y0,x1,y1) in 8x8 cells: text margins (x0+1)*8 .. x1*8, cursor at (x0+1, y0+1). 0x8424 centres a w x h
  box on cell (19, 11). 0x84A3 grows it: smaller boxes first, one retrace apart. 0x8B46 centres a string between the margins.
* Hit flash (0x2F34 -> 0x64BF): the sprite's mask is ORed into all planes, i.e. every opaque pixel turns white (15), for as
  long as the object's flash counter runs; the ammo meter (0x2222 -> 0x304F) is sprite AMMO0+ammo at 8,8 of the view.

## PC model (src/game/machine.*)
* Timer: PIT divisor 0x2147 (DS:0x2DFF) = 140.06 Hz. The interrupt (0x904B) counts DS:0x2E01, calls the hook DS:0x2E1B,
  then steps the sound: next divisor -> PIT channel 2 + speaker gate, 0 -> speaker off, 0xFFFF -> sound over.
  The front end runs two ticks per 70 Hz retrace.
* 0x8FA8 play(n): starts sound n if its priority >= the current one; the priority resets when a sound ends. F2 (DS:0x2E0B)
  only gates the speaker, the sound still runs.
* Keyboard interrupt (0x8ED1): key table DS:0x2CF6, last scan code DS:0x2D76 with bit 7 = new, ASCII DS:0x2D78.
  0x78DF clears all three; 0x8E99 reads the last key; 0x78F9 waits for a new key or a button; 0x8AAE = blinking cursor
  then the key. The PC keyboard's auto-repeat matters: after 0x78DF clears the table, a held key comes back only when it
  repeats, so the front end passes repeats on.
* Retrace wait (0x91B9 n) = n frames at 70 Hz.
* RNG index DS:0x3022 is seeded once from the DOS clock (0x9183, hundredths of a second) and never reset.

## Program
| Routine | Port | What it does |
|---|---|---|
| 0x16FE main | `Game::run` | (command line, EGA check, CTLPANEL.DD2 read, timer + keyboard hooks), then the title forever |
| 0x154E | `Game::title` | fade out, TITLE1 at x 0 and TITLE2 at x 320 of an 80-byte page, fade in; per retrace: frames 201..280 scroll right 4 px, 401..480 back, wrap at 481; F-keys; button 1/2 or SPACE starts (sound 22 LEVELSTART) |
| 0x2364 | `Game::play_game` | score 0, 4 lives, level 1; per level: fade out, intro (unless ESC is held), load, wait, fade out, play |
| 0x1201 | `Game::level_intro` | PROGPIC, box (0,0,10,3) "LEVEL n", spare Daves (lives-1, max 7) at y 0xA5 from x 0x2C step 16, score at (0xF4,0xA6), high score at (0xF4,0xB9), fade in, marker sprite 0x43 at the table DS:0x110/0x122[level], box (0,5,10,8) "LOADING" |
| 0x11D6 / 0x11B7 | `Game::wait_intro` | hook counts 20 ticks per step; waits for 20 steps (400 ticks = 200 retraces) or a key |
| 0x205F | `Game::play_loop` | per retrace: controls, objects, draw, fade in after the first frame, F12+S box, F-keys; ends on a death, level end, or F5 |
| 0x2449 | `Game::death_animation` | sound 10, then 5 pictures (knife: 4) 20 retraces apart over Dave: byte column ((x - (camx & 0xFF00)) >> 7) - 2, row ((y - (camy & 0xFF00)) >> 4) - 8. State -1 knife 0, -2 slime 5, -3 zombie 10, -4 spider 15, -5 ghost 25, -6 wolf 20, -7 skull/bosses 30, -8 hazard 35 (table 0x2729) |
| 0x24EC | (play_game) | lives - 1; at 0: grow box 17x3, "G A M E   O V E R", sound 9, wait for it, 120 retraces |
| 0x252A | (play_game) | level complete: level + 1, sound 11; after level 8 the ending |
| 0x10AA | `Game::ending` | THEEND at byte 7, row 8; 180 retraces; five 28x5 story boxes, each waits for a key |
| 0x2552 | `Game::high_score_and_continue` | score > high: box 18x11 "New High Score!", score, "Old", old high, "Congratulations!"; wait; store (CTLPANEL.DD2) |
| 0x2685 | (same) | not after F5, level 3..8: box (7,20,32,23) "Continue from level L-1 ?"; Y = new game (score 0, 4 lives) on level L-1 |

### Function keys (0x041C)
F1 help (box 30x22, 0x0239) - F2 "Sound (Y/N)?" - F3 keyboard set-up (0x7264: actions 1-4 = N/E/S/W keys, 5/6 = buttons;
the new key is the first held key found scanning the key table, LShift skipped) - F4 joystick set-up (0x6EBA; the port
skips the corner calibration, which a gamepad does not need, and keeps the "button 1 (A)ttack or (J)ump" question) -
F5 "RESET GAME (Y/N)?" - ESC "QUIT (Y/N)?" - TAB status window (in a game only: Score, High Score, Daves).
After a handled key the screen is redrawn (0x2B6D) and the key table cleared.

### Debug keys (0x0341): hold the letter, press F12
G god mode on/off (DS:0x4BDE: no ammo used, 0x44EE contact deaths cancelled; hazard tiles still kill) - W warp
("Warp to which level(1-8):", ends the level) - P pause - S single-step with a "1 tics" box (ESC ends it) - F a
1600-point bonus at Dave - T sprite test and M memory usage are not ported (developer screens).

### Controls (0x7853 -> 0x74AD keyboard)
CTLPANEL.DD2 holds eight direction scan codes N, NE, E, SE, S, SW, W, NW (default arrows + Home/PgUp/End/PgDn), button 1
(Ctrl = jump), button 2 (Alt = fire) and the high score (u32): 14 bytes, written on exit (the port also writes it when
they change). Reading order: N, E, S, W, NE, NW, SE, SW, later keys override earlier ones; index dy*3+dx+4 -> table 0x75DE.
The port adds Space/Z (jump) and X (fire) when those keys are not bound to anything else, and a gamepad.

### Sound numbers (0x8FA8 callers)
1 SHOTWALL (unused) - 2 SLIMESLOP 0x4BEA - 3 JUMP 0x3BE6/0x3EA4/0x3EEE - 4 LAND 0x46D5 - 5 SHOTMONSTER (any impact)
0x4355 - 6 SHOTMISS 0x435A - 7 NOAMMO 0x410A - 8 KNIFETHROW 0x4A3C - 9 GAMEOVER - 10 DEATHSOUND - 11 LEVELDONE -
12 OPENDOOR 0x3D6E - 13 RELOAD 0x3F38 - 14 GHOSTFADE 0x4CC2 - 15 GHOSTREAL 0x4CE5 - 16..20 TREASURE1-5 0x4598.. -
21 EXTRADAVE 0x393A/0x45DD - 22 LEVELSTART 0x163F - 23 MASTFIRE 0x5500 - 24 HITHEAD 0x46FE.

## Never reached in version 1.12
The routine at 0x6A28 (STARPIC, INTRO.DD2 = "Gamer's Edge presents" logo, floating-point zoom, "Copyright (c) 1991
Softdisk inc.", "'I' for information") has no caller; the 1.12 title is 0x154E. The port decodes INTRO.DD2
(`dd2::load_intro`) but, like the original, never shows it. `/LEVEL` (TEDLEVEL.DD2, "Exiting to ted...") is an editor
hook and is not ported.

## Port differences
* The display runs at the VGA rate of mode 0Dh, 70 Hz (on an EGA card the original ran its retrace waits at 60 Hz).
* No EGA/VGA detection, `/VER`, `/?`, `/COMP` (they print to the DOS console); the window shows 4:3 or square pixels.
* CTLPANEL.DD2 lives in the user's settings folder (`%APPDATA%\DDHM-Recomp\DDHM`), imported once from the Game folder.
* The port reads only the Assets folder (written by DDHM Launcher from the Game folder, see README). Every table of
  DAVE.EXE's data segment it uses (sprite records 0x12580, states DS:0x08BC, tile info DS:0x140C, random DS:0x3024,
  door art DS:0x0880, ray dy DS:0x13B8, kill tiers DS:0x1384) is read from there at run time (src/dd2/tables.cpp).
* F8 (soft sound), F9 (aspect), F11 / Alt+Enter (fullscreen) are port keys.
