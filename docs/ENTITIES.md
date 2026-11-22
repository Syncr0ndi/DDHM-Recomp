# Entities, objects and states (reverse-engineered from DAVE.EXE)

All addresses are offsets into the **unpacked image** (`unlzexe` output minus the 112-byte header) for code, and
**DGROUP (DS) offsets** for data. Status: CONFIRMED unless marked provisional.

## Layer-1 spawn codes
The level loader finishes by calling the spawner at image 0x5706, which walks every cell of layer 1 and dispatches
on the value (two jump tables at 0x5873 / 0x587B). Counts below were tabulated from all 8 levels.

| Code | Kind | Handler | Who | Initial state (DS) | Sprites W/E | Count |
|---|---|---|---|---|---|---|
| 0x01 | 2 | 0x47E5 | Zombie (random start frame of 4, table DS:0x13C8) | 0xDD0 | ZOMBWALKW1 / E1 | 129 |
| 0x02 | 3 | 0x49A1 | Hunchback | 0xE84 | HNCHWALKW1 / E1 | 84 |
| 0x03 | 4 | 0x4AED | Slime (only if tile below is solid) | 0xF4C | SLIMEWBOT1 / EBOT1 | 28 |
| 0x04 | 6 | 0x4C05 | Ghost | 0x1064 | GHOSTW1 / E1 | 10 |
| 0x05 | 6 | 0x4C3B | Ghost, delayed (starts invisible, then GHOSTFADE1-3) | 0xFEC | (none) | 14 |
| 0x06 | 5 | 0x50D1 | Spider | 0x1140 | SPIDERL1 / R1 | 26 |
| 0x07 | 8 | 0x4FE7 | Flaming skull | 0x1118 | SKULLFIREL1 / R1 | 4 |
| 0x08, 0x0A | - | - | no object (0x0A only sets a flag) | | | 0 |
| 0x09 | 7 | 0x4DB1 | Wolf | 0x10A0 | WOLFRUNL1 / R1 | 17 |
| 0x0B | 10 | 0x5269 | **Frank** (boss, level 4; 15 hit points) | 0x12BC | FRANKGOL1 / GOR1 | 1 |
| 0x0C | 11 | 0x53A7 | **Master** (level 8; 15 hit points) | 0x1190 | MASTER1 | 1 |
| 0x0D | 12 | 0x5693 | **Delbert**, the captive (level 8) | 0x1258 | DELBERT1 | 1 |
| 0x0E-0x13 | 9 | 0x3840 | Pickups, type = code-0x0E, state table DS:0x139C | 0xBF0, 0xC18 ... | TREASURE11 .. 61 | 132/13/11/10/10/4 |
| 0xFF | 1 | 0x3A48 | **Player start** | 0x8BC | DAVESTANDW / E | 8 (one per level) |

Pickup scores in the collect routine (0x387F): 100, 200, 400, 800, 1600 (one type awards none).

All handlers call the object constructor **0x19A8(kind, x, y, dir, state)**. Positions are fixed point:
**256 units per tile, 16 units per pixel**. Spawn x = tile_x<<8; spawn y = (tile_y<<8) + y_off where y_off is
-0x80 (zombie), +0x80 (hunchback, slime, ghosts), +0xF (player) or 0. `dir` -1 selects the west sprite, +1 the east one.
The player marker sits at the *top* tile of Dave's 2-tile-tall body.

## Object (90 bytes, constructor at 0x19A8) - partially decoded
```
+0x00 kind      +0x04 current sprite      +0x06 x   +0x08 y   (1/16 pixel)
+0x0A,+0x0C,+0x0E,+0x10  hit-box left, top, right, bottom = position + sprite record words (see below)
+0x3A direction  +0x44 pointer to current state record   +0x58 state's contact/react function
+0x3C,+0x3E,+0x48,+0x4A  AI flags/counters set by spawn handlers (e.g. 0x3E = hit points: 15 for bosses)
+0x4C pickup type (pickups only)
```
A sprite is selected as `sprite*4 + ((x>>5)&3)` - the game's pre-shifted copies; not needed in a port.

## Sprite record hit-box words - CONFIRMED
Sprite record words +0x08, +0x0A, +0x0C, +0x0E (the previously unknown `f8, fa, fc, fe`) are the hit-box
**left, top, right, bottom offsets in 1/16 pixel**. Dave standing: 64,16,240,496 = pixels x 4..15, y 1..31 of the 24x32 sprite.

## State record (20 bytes = 10 words)
```
+0  sprite when facing west     +2  sprite when facing east    +4  flag
+6  animation tics              +8  speed (e.g. zombie 128 = half a tile per step)
+10 0                           +12 think function (code address, e.g. zombie 0x4831)
+14 0                           +16 contact function (e.g. 0x3218)   +18 next state (DS offset)
```
States chain through +18 (zombie walk: 0xDD0 -> 0xDE4 -> 0xDF8 -> 0xE0C -> 0xDD0). Think/contact functions are
the enemy AI and still need to be ported.

## Non-spawn layer-1 values
Door and trigger data attached to tiles with property 1 or 2: destinations `(x<<8)|y`, level exit `0x8000`, hidden pickups
`0x8001-0x8005`. Fully decoded in **DOORS.md**.
