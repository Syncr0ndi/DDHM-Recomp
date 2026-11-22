# Doors, triggers, hidden pickups and level completion (port of DAVE.EXE, see src/game/world.* and player.*)

Status: ported and tested. All **25 door triggers** in the eight levels are validated: the destination `(x,y)` decoded from the layer-1
value is a door tile in every case, and the test suite walks Dave through every one of them (open, enter, land on solid ground).

## Tile-touch scan (0x4729, runs at the end of every frame)
Looks at every tile overlapped by Dave's box. Property `0x0D` (hazard tiles) sets the level state to -8 (death), property `0x63`
(exit zone tiles) sets it to 1 (level complete). Properties 1 and 2 with a **non-zero layer-1 value** record the trigger:
tile column/row (DS:0x91BE/0x91C0), the value (0x91C2), the tile id (0x91C4) and the property (0x91C6). The last such tile wins.

## Pressing Up (direction handler 0x3AC8)
* No door under Dave: aim up.
* Property 1 (closed door / searchable spot): `x = col*256` (snap to the door), state **DAVEOPEN** (0x970, 25 tics).
* Property 2 (open door): `x = (col-1)*256 + 0x40`, state **DAVEENTER1** (0x984) -> ENTER2 (0x998) -> ENTER3 (0x9AC), 25 tics each.
The handler only runs when the direction *changes* to up, so a door needs a fresh key press for each step.

## Open (think 0x3D62, at the end of DAVEOPEN)
* Door art animation `a = 2`; `a = 0` when the value is above 0x8000 (searchable spot); `+1` if the tile is 0x27A (634), `+2` if 0x2E4 (740).
* Value above 0x8000: a **hidden pickup** of type `value - 0x8001` (0x8001..0x8005 = types 0..4, see ENTITIES.md) appears at tile `(col, row-1)`.
* The 3 rows x 2 columns of tiles at `(col-1 .. col, row-1 .. row+1)` are overwritten from the table at DS:0x880:

| a | Tile ids (row by row) | Meaning |
|---|---|---|
| 0, 1 | 148 151 / 149 152 / 150 153 | used-up searchable spot (no property: cannot be used twice) |
| 2, 3 | 78 79 / 91 92 / 104 105 | open door (tiles 92 / 105 have property 2) |
| 4 | 833 832 / 724 725 / 737 738 | open door of the 740 type |

Dave then returns to STAND. The open door tiles have property 2, which is what makes the second Up press enter.

## Enter (think 0x3C56, at the end of DAVEENTER3)
* Value `0x8000`: **level exit** (level state 1).
* Otherwise destination `x = (value >> 8) & 0x7F`, `y = value & 0x7F` (tiles). Dave is placed at `(x*256, y*256)`, his box is recomputed, and the camera
  is re-initialised (0x1E64). He then falls the last few units onto the floor like after any move. If the destination tile does not already have
  property 2, its door is opened with the same tile rewrite (animation 2, or 3 for tile 0x27A, 4 for 0x2E4).

## Level state (DS:0x8D8E)
`0` running, `1` level complete (exit door, exit-zone tile, or Delbert's celebration on level 8), `100` boss killed (the game keeps running until everything
has settled, see ENEMIES.md), `-1..-8` death types (`-8` = hazard tile).
The port freezes the world when the state is non-zero; the viewer shows the situation. **Enter** moves on to the next level (the score, lives and
extra-life progress are kept, ammo and the level are reset); after level 8 it starts a new game. The original's death animations, bonus
screens and level-transition sequences (jump table at 0x2729) are not ported yet.

## World copy of the level
Opening doors rewrites layer-0 tiles, so `World` works on its own copy of the level (`World::level()`); `tile_version()` changes whenever tiles
change and the viewer redraws the level picture then. `restart()` reloads the pristine level, closing every door again.
