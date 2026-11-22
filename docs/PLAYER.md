# Dave: movement, shooting and collision (port of DAVE.EXE, see src/game/player.*)

Status: the logic below is **ported and unit-tested against hand-derived expectations**; it has *not* been compared with
the original running (no emulators are used in this project). Addresses: image offsets for code, DS offsets for data.

## Timing and units
* A **tic** is 1/70 s: the PIT is programmed to divisor 8519 (140.06 Hz, DS:0x2DFF set at 0x1896) and the frame loop
  (0x2739) counts two interrupts per tic. The port steps exactly one tic at a time.
* Positions and speeds: **256 units per tile, 16 units per pixel**. Sprite drawn at `((x+draw_dx)>>4, (y+draw_dy)>>4)`.
* Per-frame movement is clamped to +-256 units (0x310E).

## Object update (0x1F2A) - applies to every object, `tics` = 1 in the port
```
clear flags (on_ground, hit_head, blocked_left, blocked_right);  elapsed += 1
if state.tics <= elapsed:                       # state expired
    elapsed -= state.tics;  prev_box = box
    dx = state.x_speed * facing;  dy = state.y_speed * aim
    run think (if any);  if state unchanged -> go to state.next (or the object ends)
else if state.flag != 0:                         # "continuous" state: runs every tic
    prev_box = box;  dx = dy = 0;  run think
else: nothing
apply dx, dy (clamped), recompute box
then the state's post-move function (collision) runs
```
Stand / walk / jump / hold-aim / reload states are continuous; fire, recoil and flash states are timed.

## State machine (DS:0x08BC, 20-byte records, extracted into `player_states.inc`)
| State | Addr | tics | speed | think | post-move |
|---|---|---|---|---|---|
| STAND | 0x8BC | 6 | 0 | stand/walk (0x3B98) | ground (0x463F) |
| WALK1..8 | 0x8D0.. | 3 each | 64 per expiry (= 4 px per 3 tics = ~93 px/s) | stand/walk | ground |
| JUMP1/JUMP2 | 0x9C0/0x9D4 | 1 | 0 | air (0x437E) | air (0x46B0) |
| JUMP3 pair | 0x9E8/0x9FC | 60 / 20 | 0 | air | air |
| AIM (wind-up) | 0xA10 | 12 | -64 (recoil) | fire (0x40E7) at expiry | aim (0x4687) |
| AIM recoil, AIM end | 0xA24, 0xA38 | 10, 10 | -64, 0 | none, direction handler | aim |
| AIM N / S hold | 0xA4C / 0xA9C | 1 | 0 | aim-hold (0x3E76) | aim |
| AIM N / S fire | 0xA60.. / 0xAB0.. | 1, 15 | -32 | fire | aim |
| RELOAD | 0xAEC, 0xB00, 0xB14 | 20, 1, 20 | 0 | stand/walk, reload (0x3F34), stand/walk | reload (0x31D0) |
| door open / enter | 0x970..0x9AC | 25 | 0 | door code (not ported) | commit |
| gun flash / puff / hit flash | 0xB28.. | 10 | 64 up | none | commit |

## Controls (original input routine 0x7853 -> DS:0x8D08/0x8D0A/0x8D0C)
Direction word, clockwise from up: 0 up, 1 up-right, 2 right, 3 down-right, 4 down, 5 down-left, 6 left, 7 up-left,
**8 neutral**. A change of direction calls the handler at 0x3AB5: left/right start WALK1 and set facing, up variants enter the
aim-up hold state, down variants the aim-down hold state, neutral enters STAND. Pressing up on a door tile enters the door
sequence (**not ported yet**).

## Jump (think 0x3B98 starts it, 0x437E runs it)
* Start: `jump_timer = 14`, `vx = facing*24` if the direction is purely left/right else 0, `vy = -40`, state JUMP1.
  The button must be released before another jump (latch at DS:0x8D10).
* While `jump_timer > 0`: `dy = vy` per tic, timer decrements; releasing the button zeroes the timer (variable height).
* Afterwards gravity: `vy += 3` per tic, capped at 80, `dy += vy`. Full jump rises 14*40 + (37+34+...+1) = **807 units
  (~50 px)**; a tap rises 40 + 247 = 287 units (~18 px). `vy` is **not** reset on landing (stale value is kept).
* Air control: holding right `vx = min(vx+2, 24)`, left `vx = max(vx-2, -24)`, `dx += vx` each tic; with no direction
  held dx stays 0 (movement stops, vx is kept). Facing changes when dx changes sign.
* Landing (post-move 0x46B0): direction handler picks stand/walk, then `x += facing*64` and the box is resolved again.
  Hitting a ceiling sets `vy = 0` and enters JUMP3.

## Ground post-move (0x463F)
Horizontal resolve, ground probe; a wall hit while not in WALK1 returns to STAND; no ground -> JUMP3 (falling).

## Shooting (think 0x40E7, ray 0x3F53)
* Press fire: AIM wind-up state (12 tics). When it expires the shot happens, then recoil states move Dave back 64 units twice.
* Empty gun: click, step back 64 units, STAND. Ammo is 0..8 (DS:0x91CC), -1 per shot; standing still with ammo < 8 for
  30 tics starts the reload animation (+1 shell every 41 tics until 8).
* Muzzle (relative to the object origin): aim up `(+0x120|+0x30, +0x10)`, straight `(+0x140|+0xA0, +0xA0)`,
  down `(+0x120|+0x30, +0x150)` (first value for facing east / west).
* Straight: up to three parallel rays from y, y+0x40, y-0x80, 128 units per step, stopping at the first hit.
  Up/down: a fan of 3 (up, dy = -128/-100/-80) or 5 (down, dy = 100/80/64/48/32) rays, all cast unless an enemy is hit.
* A ray stops at solid tile faces (same face tables as movement) and gives up outside the screen
  (25 tiles right, 5 left, 14 down, 3 up of the view corner). A wall impact spawns the 16x16 HITFLASH at (impact - 0x40).
* The muzzle flash is an effect object at Dave's origin (offsets come from the flash sprites' draw offsets); flash, then two
  smoke puffs that drift up 64 units per state.
* Aim-down + jump (0x3E76) drops through a one-way floor unless the tiles below are fully solid; aim-up + jump jumps.

## Camera (0x1D90) - provisional
Dead zone: x 9..11 tiles from the left edge, y 4..8 tiles from the top. Level bounds come from the loader; the port uses
`width*256 - 20 tiles` and `height*256 - 3200`.

## Since ported
Doors and teleports (DOORS.md), enemies, damage and scoring (ENEMIES.md), death animations, HUD, menus and sound
(FLOW.md). Sounds made by Dave: JUMP (0x3BE6, 0x3EA4, 0x3EEE), LAND (0x46D5), HITHEAD (0x46FE), RELOAD per shell
(0x3F38), NOAMMO (0x410A), and after every shot SHOTMONSTER when a ray hit something / SHOTMISS otherwise (0x4355).
God mode (DS:0x4BDE) skips the ammo decrement (0x41C0).
