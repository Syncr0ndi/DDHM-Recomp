# Enemies, objects and the frame loop (port of DAVE.EXE, see src/game/object|enemy|world.*)

Status: **zombie, hunchback, knife, slime, ghost, wolf, flaming skull, spider, Frank, the Master, Delbert, gibs, score popups and pickups are ported**
(unit-tested against hand-derived expectations; not compared with the original running). **Every enemy in the game is ported**, including the bosses Frank and the Master and the captive Delbert. `FUNCTIONS.md` lists every
think/post/contact address that is still open.

## Frame structure (main loop 0x205F)
```
phase 1  for each object in index order (player = 0): if asleep, wake it when its box enters the activation
         region; if awake run the generic update (0x1F2A)            [objects created this tic start next tic]
phase 2  for each awake pair (A < B) whose boxes overlap (0x1A90, inclusive): A.contact(A,B), B.contact(B,A)
phase 3  post-move (collision) for objects in REVERSE order, the player last
then     camera (0x1D90), tile-touch checks (0x4729), drawing
```
* **Activation region** (tile units, relative to the top-left tile of the view): columns `view-2 .. view+0x17`, rows
  `view-2 .. view+0x0C`. Objects outside it sleep (frozen, no movement). An object whose post-move ends with the
  off-screen check (0x33C7: more than 8 tiles outside the view horizontally/up, 0x1C right, 0x12 down) goes back to sleep,
  and kinds >= 13 (knives, gibs, popups) are deleted instead.
* **Object update (0x1F2A)**, **state records** and the collision resolver are shared with the player: see PLAYER.md.
  The port implements them once in `Object`.
* **Hit flash**: a shot enemy gets `flash = 20` tics; the post-move functions count it down and draw the sprite in a
  white "hit" style (0x2F34). The viewer blinks the sprite instead (provisional).

## Object fields (90-byte record, work copy at DS:0x8D2A)
`+0 kind  +2 awake  +4 sprite  +6 x  +8 y  +0xA..+0x10 box  +0x12..+0x18 box tiles  +0x1A.. previous box (0x8D44)
+0x2A dx  +0x2C dy  +0x2E vx  +0x30 vy  +0x32..+0x38 blocked/ground/head flags  +0x3A facing  +0x3C aim
+0x3E hit points  +0x40 state timer  +0x42 AI delay  +0x44 state  +0x46 hit flash  +0x48 shootable  +0x4A hurtable
+0x4C timer / pickup type  +0x56 contact function  +0x58 post-move function`

## Spawning (handlers at 0x47E5.., see ENTITIES.md)
| Code | Kind | Spawn position | Facing | Hit points | Notes |
|---|---|---|---|---|---|
| 0x01 zombie | 2 | `(tx, ty*256-0x80)` | west | 2 | start frame = table DS:0x13C8 indexed by `rand/64`; aim = +1 (falls) |
| 0x02 hunchback | 3 | `(tx, ty*256+0x80)` | west | 2 | |
| 0x03 slime | 4 | floor: `+0x80`, state 0xF4C, aim +1; no floor below: state 0xF24, aim -1 | east | 1 | |
| 0x04/0x05 ghost | 6 | `+0x80` | east | 1 (not shootable until solid) | 0x04 flickers, 0x05 starts invisible (state 0xFEC), aim +1 |
| 0x06 spider | 5 | `(tx, ty*256)` | east | 1 | anchor y stored in obj+0x4C |
| 0x07 flaming skull | 8 | | east | invulnerable (shootable, not hurtable) | |
| 0x09 wolf | 7 | | east | 3 | |
| 0x0B Frank | 10 | `(tx, ty*256)` | west | 15 | |
| 0x0C Master | 11 | `(tx, ty*256)` | east | 15 | |
| 0x0D Delbert | 12 | `(tx, ty*256)` | west (aim +1) | - | harmless |
| pickups 0x0E-0x13 | 9 | `(tx, ty*256)` | west | - | type = code - 0x0E |

## Zombie (think 0x4831, post 0x3218 / 0x492A)
* Walk states 0xDD0..0xE0C: 14 tics each, 128 units per step (~40 px/s). Attack 0xE20 (8) -> 0xE34 (8) -> 0xE48 (16) -> walk.
  Fall states 0xE5C/0xE70 (20 tics, 128 units).
* Think: if Dave's feet are less than 2 tiles above/below: face him; if the horizontal gap is <= 0x80 (8 px) start the
  attack. If Dave is 1-9 rows below and within 5 columns, and there is a one-way floor exactly 2 rows under the zombie with
  no solid tile in between, **drop through it** (state 0xE5C, `dy = 0x80`).
* Post-move 0x3218 (ground walker, shared by hunchback, slime, Frank): resolve horizontally, then look at the tile row below
  the leading edge: **no floor ahead, or a wall hit -> turn around**. Turning cancels a pending AI delay, otherwise it sets
  `delay = rand/32` (0-7), during which the think function is skipped. Post-move 0x492A (falling): continue until a solid
  tile or below Dave's feet, then resolve vertically and walk again.
* Kills Dave only in its third attack frame (0xE48) and only if facing him (see contact rules).

## Hunchback (think 0x49DB, knife 0x4A72)
Same level (feet within 1 tile): if Dave is more than 7 tiles away it faces him and, with probability 50/256 per think,
winds up a throw (0xED4); if closer than 3 tiles it faces away (backs off). On another level it turns at random
(probability 2/256). The throw (at the end of 0xEFC) creates a **knife** (kind 16, state 0xF10, 32 units per tic) at
`x+0x200` (east) or `x` (west), and pauses the hunchback's AI for 4 steps. A knife vanishes in a hit flash on a wall and
is deleted off screen. Hunchbacks themselves are harmless; only the knife kills.

## Slime (think 0x4B7F, post 0x3218 floor / 0x32FD ceiling / 0x4BB9 leap)
Crawls like a zombie. On every step: 32/256 chance to reverse; 16/256 chance to leap between floor and ceiling:
from the floor it enters the wind-up 0xF9C then flies up (40 units per tic) until it hits a ceiling, then crawls upside down
(0xFC4); from the ceiling it winds up (0xF74), drops (64 units per tic) and lands (0xFD8). One hit kills (200 points). Touching = death -2.
Slimes are only 8 px tall, so a straight shot passes over them: aim down and shoot from close range (from far away the downward
rays hit the floor first).

## Ghost (think 0x4D16 / 0x4C76 / 0x4CD8, contact 0x4CEB, post 0x3172 / 0x315C - no collision, passes through walls)
* **Flickering ghost (code 4)**: alternates a visible and an invisible frame each tic (0x1064 / 0x1078) while homing in on Dave;
  not shootable. Touching Dave runs its own contact function: it turns **solid** (state 0x103C); after 30 tics its think
  (0x4CD8) makes it shootable and hurtable, and from then on touching Dave kills him (-5). One hit kills it (200 points).
* **Delayed ghost (code 5)**: waits invisible (0xFEC, checks every 5 tics). When Dave is within 2 tiles vertically and the ghost is
  **2-5 tiles behind him** (relative to his facing) it fades in over 60 tics (0x1000..0x1028), turns solid and chases.
* Homing: every tic `vx += +-1`, `vy += +-1` toward Dave, capped at 20 units per tic, `dx += vx`, `dy += vy`.

## Wolf (think 0x4DE8, post 0x4E52 run / 0x4F37 leap, leap think 0x4F33)
Runs 128-192 units per 4 tics. On Dave's level (feet within 3 tiles) it faces him and **pounces when closer than 6 tiles**:
crouch 15 tics (0x10F0), then a leap (`vy = -65`, 55 units per tic horizontally, gravity 3 per tic). A leap from 6 tiles flies
about 12 px **over** a standing Dave and lands 3-4 tiles behind him, so a wolf only hits Dave when he moves into its path.
It turns at walls and **leaps across gaps** instead of turning at ledges. After landing its AI pauses for 7 steps (0 if it hit
a wall mid-leap, in which case it lands facing back). Three hits kill it (400 points). Touching = death -6.

## Flaming skull (think 0x501A, post 0x50B8)
Flies toward Dave at up to 4 units per tic per axis, aiming at a point 0xC0 units above its own y; collides with walls. It
absorbs shots but **cannot be killed**. Touching = death -7.

## Spider (think 0x510E, post 0x32FD crawl / 0x517A drop / 0x5211 rise)
Crawls along a ceiling (like the ceiling slime) and turns to follow Dave. When Dave is within 6 tiles **below** it and within 10 px
horizontally it drops on its thread (state 0x1168: 32 units per tic; the state has sprite 0, so its 7x7 hit-box comes from sprite 0,
while the post-move draws the thread - SPIDERSILK every 0x80 units from the anchor, offset +0x40 - and the body SPIDERDOWN).
It retracts (0x117C) on touching a solid tile or after 0x600 units, and returns to crawling when back at its anchor.
One hit kills it (200 points). Touching = death -4.

## Frank (boss of level 4: think 0x52A1 / 0x52BB, post 0x3218; sparks: think 0x532C / 0x530D, post 0x5342)
* 56x80 px, **15 hit points**, shootable and hurtable; code 0x0B spawns him at `(tx, ty*256)` facing west in state 0x12BC. Touching him = death -7.
* Walks like the other ground walkers (post 0x3218: turns at walls and ledges, with the random AI pause) in 20-tic steps of 256 / 374
  units. He **does not chase Dave**. Every 11th step (think 0x52A1 counts steps in obj+0x4C) he stops (state 0x130C, 30 tics) and
  **throws a spark** (0x52BB), then keeps walking for 120 tics (0x1320).
* A **Frankspark** (kind 17, state 0x1334) appears at his hand (`box.right` facing east, `box.left - 0x100` facing west, 0x100 below his origin),
  hovers for 24 tics, then streaks along `(facing, +1)` at 24 units per tic (state 0x135C). It **ricochets** off ceilings (aim = +1), floors
  (aim = -1) and walls (facing flips), counting each bounce in obj+0x4C, and disappears after the 13th. It has 1 hit point (shootable, 0 points)
  and kills Dave on touch (-7). A shot spark leaves a SPARKCHUNK (state 0xDBC) that vanishes on any touch (think 0x399D, post 0x3A0E).
* The spark spawns at Frank's hand *before* his step in the same tic, so their boxes overlap afterwards; the shot test checks objects in order, so a shot
  at the overlap hits Frank (as in the original).
* Sleeping applies as for every enemy: if Frank walks out of the activation region he freezes until Dave comes back (level 4 is small enough that this cannot happen).
* **Boss kill** (0x342C): the two standard gibs, then every spark (kinds 0x0E, 0x0F, 0x11) is removed, the level state becomes 100 (level complete), a shower of
  **14 gib chunks** is thrown (positions/velocities in `World::kill`; one entry uses `x*2` instead of an offset, an apparent bug of the original that is kept)
  and a 1,600-point popup appears.

## The Master (boss of level 8: think 0x53DF / 0x5506, claws 0x55D9, fireballs 0x567E, post 0x3172 / 0x315C)
* 72x80 px, **15 hit points**, floats (no collision); code 0x0C spawns him at `(tx, ty*256)` facing east in state 0x1190. His body is harmless; his claws and fire kill.
  No points for killing him.
* **Patrol** (every tic): `x += 12 * facing`, turning at x = 0x200 and x = 0xF00 (tile columns 2 and 15).
* **Claws** (kind 14, 2 hit points, shootable, homing like the ghost with a speed cap kept in obj+0x4C: 8 for the left claw, 6 for the right,
  pass through walls, deadly -7, no points): while a claw is missing, a timer counts up and above 300 a new one appears - left at `(x, y+0x300)`,
  right at `(x+0x400, y+0x300)` - in state 0x11F4 (100 tics idle, then homing 0x1208/0x121C). After spawning, the Master freezes in a 60-tic pose (0x11B8).
  *A quirk of the original, kept:* the right claw's block resets the **left** timer (obj+0x54) instead of its own (obj+0x52), so a destroyed right claw
  reappears immediately (its timer is already past 300) while the left one waits 300 tics again.
* **Fire attack**: a counter (obj+0x50) reaching 330 resets the claw timer (obj+0x52) and starts the 60-tic fire pose (0x11CC); then four
  **fireballs** (kind 15, state 0x1230, constant velocity, pass through walls, deadly -7, deleted off screen) are launched:
  `(x, y+0x300)` at `(-30, +30)`, `(x+0x100, y+0x400)` at `(-16, +40)`, `(x+0x300, y+0x400)` at `(+16, +40)`, `(x+0x400, y+0x300)` at `(+30, +30)`.
  During the pose the patrol and claw logic do not run.

## Delbert (captive, level 8: states 0x1258 .. 0x12A8, posts 0x56C6 / 0x56D9, think 0x56FF)
Delbert hovers in a glass bubble at his spawn tile (code 0x0D; object index 1 on level 8, because he is the first non-player entity in scan order),
bobbing between two frames. He is harmless and cannot be shot. When the Master dies he is released (see below): he falls 64 units per tic
(0x1280, post 0x56D9: resolve vertically) until he lands, hops (0x1294: 20 tics, 320 left and 160 down), celebrates for 120 tics (0x12A8) and his think
function then sets the level state to 1 (level complete). Delbert is far above the Master (row 11 against row 33), so he sleeps until Dave climbs
back up to him; the level only ends after that.

## End of a boss fight (main loop 0x22EB..0x2344)
Killing a boss sets the level state to **100**, which keeps the game running. Every frame in state 100 the loop sets the state to 1 and then looks at all
objects except the player: if any object **other than Delbert is still active** (debris in flight, popups, ...) the state goes back to 100; when none is left
the level is complete. On **level 8** the completion is replaced by Delbert's release: the state returns to 0 and object 1's state pointer is set to 0x1280.
Objects thrown outside the activation region never wake up and therefore do not hold the level open (one of the 14 shower chunks starts at `2*x`,
which can be outside the level).

## Death of an enemy (kill routine 0x342C)
Two gib chunks (state 0xD58, kind 18): one at `(x, y)` flying west with `vx=-30, vy=-40`, one at `(x+0x100, y+0x40)` east with
`vx=+30`. Zombies and wolves add two more (0xD80: `(x, y+0x140)` and `(x+0x100, y+0x100)`). Gibs (think 0x3972): move by `vx`,
gravity as for Dave, switch to the falling frame at the apex, bounce off walls at half speed, and 50 tics after landing disappear
(post 0x39AB). Bosses remove all minions (kinds 0x0E, 0x0F, 0x11), set the level state to 100 and throw a 14-chunk gib shower (see Frank). Then the **score popup**: points tier by kind (DS:0x1384): zombie/hunchback 1 (100), slime/spider/ghost 2 (200),
wolf 3 (400), Frank 5 (1600); skull and Master none.

## Score (0x387F)
Tiers 1-5 = 100, 200, 400, 800, 1600; tier 0 = no points and the "1UP" popup. A popup (kind 18, states 0xCE0..0xD44) rises 6 units
per tic and lives 60 tics (180 for 1UP). **An extra life is awarded every 10,000 points** (score >= base + 10,000, then
base += 10,000), shown as a 1UP popup above. Start: 4 lives (DS:0x8D1E set at 0x2397), 8 shells.

## Contact rules for Dave (player contact function 0x44EE)
| Other object | Result (level state) |
|---|---|
| zombie (kind 2) in state 0xE48 | -3, unless it faces away (facing east and east of Dave, or facing west and west of him) |
| slime (4) | -2 |
| spider (5) | -4 |
| ghost (6) while solid (shootable) | -5 |
| wolf (7) | -6 |
| flaming skull (8), Frank (10), kinds 14, 15, 17 | -7 |
| knife (16) | -1 |
| hunchback (3), Master (11), Delbert (12) bodies | nothing |
| pickup (9) | collect: types 0-4 score 100..1600, type 5 = extra life (1UP popup); the pickup disappears |

In the port a negative level state freezes the world and the viewer shows it; the original's death animations
(jump table at 0x2729) are **not ported yet**, so "dead" currently just waits for R (lose a life, reload the level).

## Random numbers
`rand()` (0x91A5) returns `table[++index & 255]` from a 256-byte table at DS:0x3024 (index starts at 0); the port embeds the
same table, so random decisions follow the original sequence.
