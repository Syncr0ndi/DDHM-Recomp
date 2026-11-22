# Function address reference (generated from the state table)

Every code address referenced by an object state record, with the states that use it. `think` runs when a state expires
(or every tic for continuous states), `post` is the post-move / collision function, `contact` the overlap function.
Ported ones are marked **ported**; the rest belong to Frank, the Master and Delbert (next step) and the door sequences.

## think

| Address | Status | Used by states |
|---|---|---|
| 0x3414 | **ported** | 0xE20 ZATTACKW1, 0xE34 ZATTACKW2, 0xE48 ZATTACKW3, 0xE5C ZOMBDOWN1, 0xE70 ZOMBDOWN2, 0xED4 HNATTACKW1, ... (7 states) |
| 0x380B | **ported** | 0xCE0 BONUS100, 0xCF4 BONUS200, 0xD08 BONUS400, 0xD1C BONUS800, 0xD30 BONUS1600 |
| 0x3825 | **ported** | 0xD44 BONUS1UP |
| 0x3972 | **ported** | 0xD58 CHUNKNW1, 0xD6C CHUNKNW2, 0xD80 CHUNKSW1, 0xD94 CHUNKSW2 |
| 0x399D | **ported** | 0xDBC SPARKCHUNK |
| 0x3B98 | **ported** | 0x8BC DAVESTANDW, 0x8D0 DAVEWALKW1, 0x8E4 DAVEWALKW1, 0x8F8 DAVEWALKW2, 0x90C DAVEWALKW2, 0x920 DAVEWALKW3, ... (11 states) |
| 0x3C56 | **ported** | 0x9AC DAVEENTER3 |
| 0x3D62 | **ported** | 0x970 DAVEOPEN |
| 0x3E76 | **ported** | 0xA4C AIMNW1, 0xA9C AIMSW1 |
| 0x3F34 | **ported** | 0xB00 RELOAD2 |
| 0x40E7 | **ported** | 0xA10 AIMW1, 0xA60 AIMNW1, 0xAB0 AIMSW1 |
| 0x437A | **ported** | 0xA38 AIMW2, 0xA88 AIMNW2, 0xAD8 AIMSW2 |
| 0x437E | **ported** | 0x9C0 DAVEJUMPW1, 0x9D4 DAVEJUMPW2, 0x9E8 DAVEJUMPW3, 0x9FC DAVEJUMPW2 |
| 0x4831 | **ported** | 0xDD0 ZOMBWALKW1, 0xDE4 ZOMBWALKW2, 0xDF8 ZOMBWALKW3, 0xE0C ZOMBWALKW4 |
| 0x49DB | **ported** | 0xE84 HNCHWALKW1, 0xE98 HNCHWALKW2, 0xEAC HNCHWALKW3, 0xEC0 HNCHWALKW4 |
| 0x4A72 | **ported** | 0xEFC HNATTACKW3 |
| 0x4B7F | **ported** | 0xF24 SLIMEWTOP1, 0xF38 SLIMEWTOP2, 0xF4C SLIMEWBOT1, 0xF60 SLIMEWBOT2 |
| 0x4C76 | **ported** | 0xFEC - |
| 0x4CD8 | **ported** | 0x103C GHOSTSOLID1, 0x1050 GHOSTSOLID2 |
| 0x4D16 | **ported** | 0x1064 GHOSTW1, 0x1078 -, 0x108C GHOSTW1 |
| 0x4DE8 | **ported** | 0x10A0 WOLFRUNL1, 0x10B4 WOLFRUNL2, 0x10C8 WOLFRUNL3, 0x10DC WOLFRUNL4 |
| 0x4F33 | **ported** | 0x1104 WOLFJUMPL |
| 0x501A | **ported** | 0x1118 SKULLFIREL1, 0x112C SKULLFIREL2 |
| 0x510E | **ported** | 0x1140 SPIDERL1, 0x1154 SPIDERL2 |
| 0x52A1 | **ported** | 0x12BC FRANKGOL1, 0x12D0 FRANKGOL2, 0x12E4 FRANKGOL3, 0x12F8 FRANKGOL4 |
| 0x52BB | **ported** | 0x130C FRANKGOL1 |
| 0x530D | **ported** | 0x135C FRANKSPARK1, 0x1370 FRANKSPARK2 |
| 0x532C | **ported** | 0x1334 FRANKSPARK1, 0x1348 FRANKSPARK2 |
| 0x53DF | **ported** | 0x1190 MASTER1, 0x11A4 MASTER1, 0x11B8 MASTER1 |
| 0x5506 | **ported** | 0x11CC MASTER1, 0x11E0 MASTER2 |
| 0x55D9 | **ported** | 0x1208 MASTCLAW1, 0x121C MASTCLAW2 |
| 0x567E | **ported** | 0x1230 MASTFIRE1, 0x1244 MASTFIRE2 |
| 0x56FF | **ported** | 0x12A8 DELBERT4 |

## post

| Address | Status | Used by states |
|---|---|---|
| 0x315C | **ported** | 0x970 DAVEOPEN, 0x984 DAVEENTER1, 0x998 DAVEENTER2, 0x9AC DAVEENTER3, 0xB28 GUNFLASHNW, 0xB3C GUNPUFFNW, ... (44 states) |
| 0x3172 | **ported** | 0x1190 MASTER1, 0x11A4 MASTER1, 0x11B8 MASTER1, 0x11CC MASTER1, 0x11E0 MASTER2, 0x11F4 MASTCLAW1, ... (8 states) |
| 0x31B7 | **ported** | 0xF74 SLIMERDYSW, 0xF9C SLIMERDYNW |
| 0x31D0 | **ported** | 0xAEC RELOAD1, 0xB00 RELOAD2, 0xB14 RELOAD2, 0x10F0 WOLFRUNL4 |
| 0x3218 | **ported** | 0xDD0 ZOMBWALKW1, 0xDE4 ZOMBWALKW2, 0xDF8 ZOMBWALKW3, 0xE0C ZOMBWALKW4, 0xE20 ZATTACKW1, 0xE34 ZATTACKW2, ... (22 states) |
| 0x32FD | **ported** | 0xF24 SLIMEWTOP1, 0xF38 SLIMEWTOP2, 0x1140 SPIDERL1, 0x1154 SPIDERL2 |
| 0x39AB | **ported** | 0xD58 CHUNKNW1, 0xD6C CHUNKNW2, 0xD80 CHUNKSW1, 0xD94 CHUNKSW2 |
| 0x3A0E | **ported** | 0xDBC SPARKCHUNK |
| 0x463F | **ported** | 0x8BC DAVESTANDW, 0x8D0 DAVEWALKW1, 0x8E4 DAVEWALKW1, 0x8F8 DAVEWALKW2, 0x90C DAVEWALKW2, 0x920 DAVEWALKW3, ... (9 states) |
| 0x4687 | **ported** | 0xA10 AIMW1, 0xA24 AIMW1, 0xA38 AIMW2, 0xA4C AIMNW1, 0xA60 AIMNW1, 0xA74 AIMNW1, ... (11 states) |
| 0x46B0 | **ported** | 0x9C0 DAVEJUMPW1, 0x9D4 DAVEJUMPW2, 0x9E8 DAVEJUMPW3, 0x9FC DAVEJUMPW2 |
| 0x492A | **ported** | 0xE5C ZOMBDOWN1, 0xE70 ZOMBDOWN2 |
| 0x4ABD | **ported** | 0xF10 KNIFEW |
| 0x4BB9 | **ported** | 0xF88 SLIMEJMPSW, 0xFB0 SLIMEJMPNW |
| 0x4E52 | **ported** | 0x10A0 WOLFRUNL1, 0x10B4 WOLFRUNL2, 0x10C8 WOLFRUNL3, 0x10DC WOLFRUNL4 |
| 0x4F37 | **ported** | 0x1104 WOLFJUMPL |
| 0x50B8 | **ported** | 0x1118 SKULLFIREL1, 0x112C SKULLFIREL2 |
| 0x517A | **ported** | 0x1168 - |
| 0x5211 | **ported** | 0x117C - |
| 0x5342 | **ported** | 0x1334 FRANKSPARK1, 0x1348 FRANKSPARK2, 0x135C FRANKSPARK1, 0x1370 FRANKSPARK2 |
| 0x56C6 | **ported** | 0x1258 DELBERT1, 0x126C DELBERT1, 0x1294 DELBERT3, 0x12A8 DELBERT4 |
| 0x56D9 | **ported** | 0x1280 DELBERT1 |

## contact

| Address | Status | Used by states |
|---|---|---|
| 0x44EE | **ported** | 0x8BC DAVESTANDW, 0x8D0 DAVEWALKW1, 0x8E4 DAVEWALKW1, 0x8F8 DAVEWALKW2, 0x90C DAVEWALKW2, 0x920 DAVEWALKW3, ... (28 states) |
| 0x4CEB | **ported** | 0x1064 GHOSTW1, 0x1078 - |


## Program routines (not referenced by states)

All ported; details and the port function for each are in **FLOW.md**.

| Address | Routine |
|---|---|
| 0x0239 / 0x041C / 0x0341 | F1 help / function-key dispatcher / F12 debug keys |
| 0x055B (in 0x041C) | TAB status window |
| 0x10AA / 0x1201 / 0x11D6 | ending / level intro / intro wait |
| 0x154E / 0x2364 / 0x205F | title / one game / play loop |
| 0x2449..0x24EA / 0x24EC / 0x2552 / 0x2685 | death animation / game over / high score / continue |
| 0x2222 -> 0x304F, 0x2F34 -> 0x64BF | ammo meter, white hit flash |
| 0x6EBA / 0x7264 / 0x70FD | F4 joystick set-up / F3 keyboard set-up / key names |
| 0x74AD / 0x7853 / 0x78DF / 0x78F9 / 0x8AAE / 0x8E99 | keyboard read / control read / clear keys / wait key / prompt / read key |
| 0x7B04 + 0x7EFC | RLEB file loader (INTRO.DD2) |
| 0x8208 / 0x8261 | fade in / fade out |
| 0x82BE / 0x8424 / 0x84A3 / 0x8A61 / 0x8B46 / 0x963D | window / centred window / growing window / print / print centred / glyph |
| 0x8D6F / 0x8E01 | CTLPANEL.DD2 read / write |
| 0x8ED1 / 0x904B | keyboard interrupt / timer interrupt with the PC-speaker player |
| 0x8FA8 / 0x8FDE / 0x9033 | play sound / stop sound / wait for the sound |
| 0x9183 / 0x91B9 | RNG seed / retrace wait |
| 0x9450 / 0x94EE | tile8 / picture draw |
