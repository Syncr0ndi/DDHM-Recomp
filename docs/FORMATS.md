# Dangerous Dave in the Haunted Mansion - reverse-engineered formats

Status key: **CONFIRMED** = decoded and verified visually and/or by exact length checks
(`tests/test_assets.cpp`); **PARTIAL** = understood in part; **OPEN** = not yet understood.

## Executables - CONFIRMED packing
`DAVE.EXE`, `DOCSHELL.EXE`, `RoomDoom/ROOMS.EXE`, `RoomDoom/NEWINTRO.EXE` are all LZEXE 0.91 packed
(`LZ91` at 0x1C). `tools/unlzexe.c` unpacks them. Verified: entry point is the standard Borland C
startup stub, 21 relocations all hold coherent segment values, and "Borland C++ - Copyright 1991
Borland Intl." appears in the unpacked image.

Unpacked `DAVE.EXE` (215,472 bytes, 112-byte header): DGROUP segment value `0x2713`; SS:SP `348C:0080`.
Observed image layout (offsets from start of image, not file): dense code-like bytes in the first ~64 KB;
1bpp bitmap font data from ~0x10000; an embedded RLEW-compressed level fragment near 0x29776;
zero-filled BSS from ~0x2C000. Code/data boundaries are **not yet established** - needs a real disassembler.

## EGATILES.DD2 - CONFIRMED
Headerless array of 858 tiles. Each tile is 16x16 pixels, 128 bytes: four bit planes
(plane 0 = blue ... plane 3 = intensity), each plane 16 rows x 2 bytes, MSB = leftmost pixel.
Pixel colour index = bit0|bit1<<1|bit2<<2|bit3<<3 into the standard 16-colour EGA palette.

## LEVELnn.DD2 - CONFIRMED
```
u32  expanded_length
...  RLEW stream (tag 0xFEFE: tag, u16 count, u16 value), expanding to expanded_length bytes
```
Expanded data:
```
u16 header[16]
  [0] width (tiles)   [1] height (tiles)   [2] layers (=2)
  [3],[4] = 0         [5],[6] = 80,42 (big levels) or 40,24 (small)
  [7] bytes per layer (= width*height*2, except LEVEL06 = 4 cells more)
  [8..15] identical in every level
  The game reads only [0], [1], [2] and [7] (loader 0x0E66..0x0EA7 and 0x0775); the other words are never read by
  DAVE.EXE - CONFIRMED unused (most likely written by the level editor).
u16 layer0[stride]   tile indices into EGATILES (0..857), row-major
u16 layer1[stride]   entity / spawn codes (mostly 0)
```
`expanded_length == 32 + layers*header[7]` holds for all eight levels.

Layer-1 entity codes are decoded - see **ENTITIES.md** (spawn table, object and state structures).
LEVEL06 has a layer stride 4 cells longer than width*height and an unused black strip at the right edge;
both are authentic data, not decoder bugs.
## PIC images - CONFIRMED
`'PIC\0'`, u16 bytes-per-row (40), u16 rows (200), then 4 planes x (rows x bytes-per-row), same plane
convention as tiles. `TITLE1/2.DD2` are raw PIC; `STARPIC.DD2` and `PROGPIC.DD2` are HUFF-wrapped PIC.

## HUFF container - CONFIRMED
```
"HUFF", u32 expanded_length
255 nodes x { u16 child0, u16 child1 }     // value < 0x100: literal byte; >= 0x100: node (value-0x100)
bitstream, consumed LSB-first, restart at the root (node 254) after every literal
```
Every Huffman file in the game is consumed exactly to its last byte with this decoder.

## S_DAVE / S_CHUNK1 / S_CHUNK2 / S_FRANK / S_MASTER - CONFIRMED
After HUFF expansion: `u16 plane_size`, then **5 planes** of `plane_size` bytes each (expanded length = 2 + 5*plane_size).
Planes 0-3 are the EGA colour planes (same bit convention as tiles); **plane 4 is a mask, 1 = transparent**
(the game fills bits shifted in during its pre-shift pass with 0xFF only for the fifth plane).
Within each plane the sprites follow each other in table order, each `width_bytes*height` bytes,
rows top to bottom, MSB = leftmost pixel. For every file the sum of `width_bytes*height` over its sprites equals
`plane_size` exactly (verified for all five files).

Sprite files and their table ranges (loader jump table at image 0x9bd; filename pointers 0x386.. in DGROUP):
S_DAVE 0-84, S_CHUNK1 85-120, S_CHUNK2 121-173, S_FRANK 174-186, S_MASTER 187-198 (199 sprites in total).

### Sprite definition block (inside DAVE.EXE) - CONFIRMED
Names and sizes are **not** in the .DD2 files. The EXE contains a data block at image offset 0x122B0 (segment 0x122B,
set up by the init routine at 0x7B5). Header word 0x0C >> 4 gives the paragraph offset of the record table
(image 0x12580), word 0x2E = 199 (sprites). The table has 4 records of 32 bytes per sprite:
the first is the real sprite, the other three are the pre-shifted copies the DOS game builds at startup
(width+1 bytes) for smooth horizontal drawing - a modern renderer does not need them.
```
+0x00 u16 width in bytes     +0x02 u16 height in rows
+0x04 (8 for sprites, 0 for shifted copies), +0x06 paragraph offset
+0x08,+0x0A,+0x0C,+0x0E  hit-box left, top, right, bottom in 1/16 pixel - CONFIRMED (see ENTITIES.md)
+0x1C,+0x1E  draw offset x, y in 1/16 pixel - CONFIRMED: added to the object position when drawing and also
             included in the hit-box (e.g. AIMW1 is 32 px wide with dx = -128 = 8 px to the left)
+0x10 char[16] name          (STARTDAVE, DAVESTANDE, DAVEWALKE1..4, DAVEJUMPE1..3, RELOAD1, AMMO0..8, AIME1, ...)
+0x1A (computed at startup) width*height = bytes per plane
```
`src/dd2/sprite_table.inc` reproduces name, file, width and height plus the raw words for all 199 sprites.
The game also has a built-in "Sprite Test" screen (strings "Sprite:", "Width:", "Height:", "Source:").

## INTRO.DD2 - CONFIRMED
Byte RLE ("RLEB", loader 0x7B04, expander 0x7EFC): `u32 expanded_length` (0x2000), then bytes; `0xFE count value`
writes `value` count times, any other byte is a literal. The 8192 bytes are four 32x64-byte colour planes: a 256x64
picture, the "Gamer's Edge presents" logo. Only the intro routine at 0x6A28 (with STARPIC.DD2) reads it, and nothing
in DAVE.EXE 1.12 calls that routine, so the game never shows it (see FLOW.md). `dd2::load_intro`, `dd2tool` exports it.

## Data inside DAVE.EXE - CONFIRMED
Window tiles, the proportional font, the death / ending pictures, the sound effects and all texts are in the
executable, not in .DD2 files. Offsets and formats: FLOW.md, "Data that lives inside DAVE.EXE".
Sprite record words +4/+6 (the `f4`/`f6` columns of sprite_table.inc) are copied to DS:0x9908/0x990A with the record
and never read - CONFIRMED unused by the game.

## Rooms of Doom (1993 re-release)
Levels 1-8, EGATILES and the S_*.DD2 files are byte-identical to the Softdisk release, so one asset
loader serves both. `ROOMS.EXE` replaces `DAVE.EXE`.
