# Tile behaviour, collision and level state (reverse-engineered from DAVE.EXE)

Addresses: image offsets for code, DS offsets for data. Status CONFIRMED unless marked provisional.

## Tile info block - CONFIRMED
Initialised data at **DS:0x140C** (set up by the routine at image 0x723). Header words: `+0` = 858 (tile count),
`+2` = 6 (tables), `+4..+0x0E` = byte offsets (from 0x140C) of six consecutive tables of **858 bytes, one per tile**:

| # | DS address | Global pointer | Meaning | Tiles flagged |
|---|---|---|---|---|
| 0 | 0x144A | DS:0x4BE4 | **Top face solid** (floor: blocks downward movement; one-way when approached from below/side) | 145 |
| 1 | 0x17A4 | DS:0x4BE6 | **Bottom face solid** (ceiling: blocks upward movement) | 68 |
| 2 | 0x1AFE | DS:0x4F2A | **Right face solid** (blocks an object moving left) | 68 |
| 3 | 0x1E58 | DS:0x4F30 | **Left face solid** (blocks an object moving right) | 68 |
| 4 | 0x21B2 | DS:0x4F42 | unused (all zero; the pointer is set at 0x0723 and never read) | 0 |
| 5 | 0x250C | DS:0x535A | **Property code** | 34 |

Tables 1, 2 and 3 are identical and a subset of table 0: 68 fully solid tiles plus 77 one-way floor tiles
(platforms, the climbable rope on the mansion's outer wall). `src/dd2/tileinfo_data.inc` holds the lists;
the API is `tile_face_solid()` / `tile_property()`.

## Collision resolver - CONFIRMED (port target)
Called as horizontal (0x1C7D) then vertical (0x1BA2) on the object's hit-box. All values are 1/256-tile units.
Working copy of the box at DS:0x8D30.. (x, left, right at 0x8D30/0x8D34/0x8D38; y, top, bottom at 0x8D32/0x8D36/0x8D3A);
tile bounds of the new box: left col, top row, right col, bottom row; `prev_*` are the bounds before the move.
```
horizontal:
  for col in prev_right+1 .. right:   for row in top..bottom:
      if LEFT_FACE[tile(col,row)]:  x -= (right+1) - col*256;  blocked_right = true; stop
  for col in prev_left-1 down to left: for row in top..bottom:
      if RIGHT_FACE[tile(col,row)]: x += (col+1)*256 - left;   blocked_left = true;  stop
vertical:
  for row in prev_bottom+1 .. bottom: for col in left..right:
      if TOP_FACE[tile(col,row)]:   y -= (bottom+1) - row*256; on_ground = true; stop
  for row in prev_top-1 down to top:  for col in left..right:
      if BOTTOM_FACE[tile(col,row)]: y += (row+1)*256 - top;   hit_head = true;  stop
ground probe (0x1D51): if the box bottom has bit0 set, look at the tiles in the row below the CURRENT bottom row
  (cols left..right): any TOP_FACE -> on_ground. (An object resting on a floor has bottom = row*256-1, i.e. odd.)
```
Only tiles *beyond the previous bounds* can push the object, which is what makes floors one-way and prevents
getting stuck on tiles the object already overlaps.

## Tile property codes
| Code | Tiles | Behaviour |
|---|---|---|
| 1 | 265, 597, 634, 636, 740 | Door: layer-1 value on the tile = destination, see below |
| 2 | 49, 92, 725, 738 | Door-like, handled without the open animation |
| 4 | 316 | stair-like tile; never tested by the game |
| 0x0D | 122, 123, 135, 136 | **Hazard** (bloody trap tiles): sets level state -8 (death) |
| 0x63 | 17, 145, 545 | **Exit zone**: sets level state 1 (level complete) |
| 0x34-0x36, 0x69-0x6D, 0x75, 0xFE | 304-336 (various) | never tested by the game (editor-only attributes) |

CONFIRMED exhaustive: the property table pointer DS:0x535A is read in exactly two places, the door handler 0x3CB8
(code 2) and the tile-touch scan 0x475B (codes 0x0D, 1, 2, 0x63); no code addresses the table directly.

## Level state variable (DS:0x8D8E) - provisional
Written by tile touches and by object code, read by the level loop (image 0x22EB, 0x2452):
`0` running, `1` level exit, `0x64` (100) boss killed (set by the kill routine 0x342C for Frank / the Master),
negative `-8..-1` = death types (jump table at 0x2729 selects the death animation/sequence).

## Door and trigger values in layer 1
Fully decoded and ported: see **DOORS.md** (open, enter, teleport destinations, hidden pickups, level exit).
