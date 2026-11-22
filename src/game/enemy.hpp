// SPDX-License-Identifier: MIT
// enemy.hpp - enemies, projectiles, pickups, gibs and score popups (every non-player object).
// Ported from the DOS executable's spawn handlers (0x47E5...) and per-kind think / post-move functions;
// see docs/ENEMIES.md for the address of every routine. Enemy kinds whose AI is not ported yet are created
// (so levels look complete) but stay inert: they are drawn, cannot be shot and cannot hurt Dave.
#pragma once

#include <cstdint>
#include <vector>

#include "game/object.hpp"
#include "game/player.hpp"

namespace ddhm {

class World;

// Sprites an enemy draws by itself in addition to its own (the spider's thread and body).
struct ExtraDraw { int sprite, x, y; };

enum Kind : int {
    kKindNone = 0, kKindPlayer = 1, kKindZombie = 2, kKindHunchback = 3, kKindSlime = 4, kKindSpider = 5,
    kKindGhost = 6, kKindWolf = 7, kKindSkull = 8, kKindPickup = 9, kKindFrank = 10, kKindMaster = 11,
    kKindDelbert = 12, kKindMasterClaw = 14, kKindMasterFire = 15, kKindKnife = 16, kKindSpark = 17, kKindDebris = 18,
};

// The game's random number generator (0x91A5): a 256-entry table walked by an index.
class Rng {
public:
    int next();
    void seed(int index) { idx_ = index & 0xFF; }     // DS:0x3022 (0x9183 seeds it from the clock at start-up)
private:
    int idx_ = 0;
};

struct EnemyContext {
    const Player* player = nullptr;
    World* world = nullptr;
    Rng* rng = nullptr;
    int view_col = 0, view_row = 0;       // top-left tile of the screen (DS:0x8D9A / 0x8DBE)
};

class Enemy : public Object {
public:
    void init(int kind, int x, int y, int dir, uint16_t state_ds);    // the object constructor (0x19A8)
    void bind(EnemyContext* c) { ctx_ = c; }

    int kind = 0;
    bool active = false;                 // awake (inside the activation region)
    bool shootable = false;              // obj+0x48
    bool hurtable = false;               // obj+0x4A
    bool implemented = false;            // AI ported; false = inert placeholder
    int hp = 0;                          // obj+0x3E
    int flash = 0;                       // hit-flash tics remaining (obj+0x46)
    int timer = 0;                       // obj+0x4C: popup lifetime / spider anchor / boss counters
    int age = 0;                         // obj+0x4E
    uint32_t id = 0;                     // unique id (the original keeps raw object pointers)
    uint32_t link_a = 0, link_b = 0;     // Master: ids of his left / right claw (obj+0x4C / +0x4E)
    int t50 = 0, t52 = 0, t54 = 0;       // Master: fire timer, right-claw timer, left-claw timer (obj+0x50/52/54)
    int pickup_type = 0;

    void set_velocity(int vx, int vy) { vx_ = vx; vy_ = vy; }
    void set_aim(int a) { aim_ = a; }
    bool wakes_up(int view_col, int view_row) const;                  // activation region test (0x20D1)
    void remove() { kind = 0; active = false; }
    void force_state(uint16_t ds) { set_state(ds); }                   // used by the world (Delbert's release)
    void set_facing(int f) { facing_ = f; }
    void touch_player();                                                // contact function of the ghost (0x4CEB)
    void extra_draws(std::vector<ExtraDraw>& out) const;

protected:
    void run_think(uint16_t think, const dd2::Level&) override;
    void run_post(uint16_t post, const dd2::Level&) override;

private:
    void face_player();
    void think_zombie(const dd2::Level&);
    void think_hunchback();
    void throw_knife();
    void think_gib();
    void think_popup(int lifetime);
    void post_walker(const dd2::Level&);
    void post_zombie_down(const dd2::Level&);
    void post_knife(const dd2::Level&);
    void post_gib(const dd2::Level&);
    void post_ceiling_walker(const dd2::Level&);
    void post_resolve_commit(const dd2::Level&);
    void post_slime_jump(const dd2::Level&);
    void post_wolf_run(const dd2::Level&);
    void post_wolf_jump(const dd2::Level&);
    void post_spider_drop(const dd2::Level&);
    void post_spider_rise(const dd2::Level&);
    void think_frank_step();
    void think_frank_throw();
    void think_spark_hover();
    void post_spark(const dd2::Level&);
    void post_spark_gib(const dd2::Level&);
    void think_master();
    void think_master_fire();
    void post_delbert_fall(const dd2::Level&);
    void think_slime();
    void think_ghost_wait();
    void think_homing(int max_speed, int y_target_offset);
    void think_wolf();
    void think_spider();
    void turn_around(const dd2::Level&);
    void tick_flash();
    void offscreen_check();

    EnemyContext* ctx_ = nullptr;
    bool bumped_ = false;                 // wolf: hit a wall during a leap (obj+0x52)
};

}  // namespace ddhm
