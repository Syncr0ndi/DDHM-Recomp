// SPDX-License-Identifier: MIT
#include "game/world.hpp"

#include <algorithm>

namespace ddhm {

World::World() {
    ctx_.player = &player_;
    ctx_.world = this;
    ctx_.rng = &rng_;
    player_.set_shot_target(this);
    player_.set_sound_sink(&sounds_);
}

void World::new_game() {
    score_ = 0;
    extra_life_base_ = 0;
    lives_ = 4;
}

void World::load(const dd2::Level& level, int level_number) {
    source_ = &level;
    level_number_ = level_number;
    next_id_ = 1;
    level_copy_ = level;
    level_ = &level_copy_;
    tile_version_ = 0;
    door_ = DoorTouch{};
    level_state_ = 0;
    objs_.clear();
    if (reset_rng_on_load) rng_ = Rng();
    spawn_level_objects();
}

void World::restart() {
    --lives_;
    if (source_) load(*source_, level_number_);
}

Enemy& World::spawn(int kind, int x, int y, int dir, uint16_t state_ds) {
    objs_.emplace_back();
    Enemy& e = objs_.back();
    e.bind(&ctx_);
    e.init(kind, x, y, dir, state_ds);
    e.id = next_id_++;
    return e;
}

Enemy* World::find_alive(uint32_t id, int kind) {
    if (id == 0) return nullptr;
    for (Enemy& e : objs_)
        if (e.id == id && e.kind == kind) return &e;
    return nullptr;
}

void World::spawn_level_objects() {
    const dd2::EntityList ents = dd2::extract_entities(*source_);
    static const uint16_t kZombieStates[4] = {0xDD0, 0xDE4, 0xDF8, 0xE0C};
    for (const dd2::Spawn& s : ents.spawns) {
        const dd2::SpawnDef& d = *s.def;
        const int tx = s.tile_x << 8, ty = s.tile_y << 8;
        if (d.code == 0xFF) { player_.spawn(s.tile_x, s.tile_y, *level_); continue; }
        if (d.code == 0x01) {                                       // zombie: random walk frame
            Enemy& e = spawn(kKindZombie, tx, ty - 0x80, -1, kZombieStates[rng_.next() / 0x40]);
            e.shootable = e.hurtable = e.implemented = true;
            e.hp = 2;
            e.set_aim(1);
        } else if (d.code == 0x02) {                                // hunchback
            Enemy& e = spawn(kKindHunchback, tx, ty + 0x80, -1, 0xE84);
            e.shootable = e.hurtable = e.implemented = true;
            e.hp = 2;
        } else if (d.code >= 0x0E && d.code <= 0x13) {              // pickups
            Enemy& e = spawn(kKindPickup, tx, ty, -1, d.state_ptr);
            e.pickup_type = d.code - 0x0E;
            e.implemented = true;
        } else if (d.code == 0x03) {                                // slime: floor or ceiling variant (inert)
            if (dd2::tile_face_solid(tile_at(*level_, s.tile_x, s.tile_y + 1), dd2::TileFace::Top)) {
                Enemy& e = spawn(d.kind, tx, ty + 0x80, 1, 0xF4C);
                e.set_aim(1);
                e.shootable = e.hurtable = e.implemented = true;
                e.hp = 1;
            } else {
                Enemy& e = spawn(d.kind, tx, ty, 1, 0xF24);
                e.set_aim(-1);
                e.shootable = e.hurtable = e.implemented = true;
                e.hp = 1;
            }
        } else if (d.code == 0x04 || d.code == 0x05) {              // ghosts: untouchable until solid
            Enemy& e = spawn(d.kind, tx, ty + 0x80, 1, d.state_ptr);
            e.implemented = true;
            e.hp = 1;
            if (d.code == 0x05) e.set_aim(1);
        } else if (d.code == 0x06) {                                // spider hangs from its anchor
            Enemy& e = spawn(d.kind, tx, ty, 1, 0x1140);
            e.shootable = e.hurtable = e.implemented = true;
            e.hp = 1;
            e.timer = e.y();                                        // anchor (obj+0x4C)
        } else if (d.code == 0x07) {                                // flaming skull: shootable, invulnerable
            Enemy& e = spawn(d.kind, tx, ty, 1, 0x1118);
            e.shootable = e.implemented = true;
        } else if (d.code == 0x0B) {                                // Frank (boss of level 4)
            Enemy& e = spawn(d.kind, tx, ty, -1, 0x12BC);
            e.shootable = e.hurtable = e.implemented = true;
            e.hp = 15;
        } else if (d.code == 0x0C) {                                // the Master (boss of level 8)
            Enemy& e = spawn(d.kind, tx, ty, 1, 0x1190);
            e.shootable = e.hurtable = e.implemented = true;
            e.hp = 15;
        } else if (d.code == 0x0D) {                                // Delbert, the captive
            Enemy& e = spawn(d.kind, tx, ty, 1, 0x1258);
            e.set_facing(-1);
            e.set_aim(1);
            e.implemented = true;
        } else if (d.code == 0x09) {                                // wolf
            Enemy& e = spawn(d.kind, tx, ty, 1, 0x10A0);
            e.shootable = e.hurtable = e.implemented = true;
            e.hp = 3;
        } else {                                                    // everything else: placeholder, inert
            spawn(d.kind, tx, ty + d.y_off, d.dir, d.state_ptr);
        }
    }
}

bool World::hit(int x, int y) {
    for (size_t i = 0; i < objs_.size(); ++i) {
        Enemy& e = objs_[i];
        if (!e.kind || !e.active || !e.shootable) continue;
        const Box& b = e.box();
        if (b.l > x || b.r < x || b.t > y || b.b < y) continue;
        if (e.hurtable) {
            e.flash = 20;
            if (--e.hp == 0) kill(e);
        }
        return true;
    }
    return false;
}

void World::score_popup(int x, int y, int tier) {
    static const uint16_t kPopupState[6] = {0xD44, 0xCE0, 0xCF4, 0xD08, 0xD1C, 0xD30};
    static const uint32_t kPoints[6] = {0, 100, 200, 400, 800, 1600};
    if (tier < 0 || tier > 5) return;
    score_ += kPoints[tier];
    Enemy& p = spawn(kKindDebris, x, y, 0, kPopupState[tier]);
    p.active = p.implemented = true;
    p.set_aim(-1);
    if (extra_life_base_ + 10000 <= score_) {                       // extra life every 10,000 points
        extra_life_base_ += 10000;
        ++lives_;
        sound(21);                                                  // EXTRADAVE (0x393A)
        Enemy& q = spawn(kKindDebris, x, y - 0x100, 0, 0xD44);
        q.active = q.implemented = true;
        q.set_aim(-1);
    }
}

void World::kill(Enemy& e) {                                        // original 0x342C
    const int ex = e.x(), ey = e.y(), kind = e.kind;
    auto gib = [&](uint16_t st, int dir, int gx, int gy, int vx, int vy) {
        Enemy& g = spawn(kKindDebris, gx, gy, dir, st);
        g.set_velocity(vx, vy);
        g.implemented = true;            // wakes up through the normal activation test (a chunk thrown outside the
    };                                   // screen region never wakes, exactly as in the original)
    gib(0xD58, -1, ex, ey, -30, -40);
    gib(0xD58, 1, ex + 0x100, ey + 0x40, 30, -40);
    if (kind == kKindFrank || kind == kKindMaster) {
        for (Enemy& o : objs_)                                      // the boss's minions vanish
            if (o.kind == kKindSpark || o.kind == kKindMasterFire || o.kind == kKindMasterClaw) o.remove();
        level_state_ = 100;
        // The gib shower of the original (0x3534..0x3788): {state, dir, dx, dy, vx, vy} relative to the boss origin.
        struct G { uint16_t st; int dir, dx, dy, vx, vy; };
        static const G kShower[14] = {
            {0xD80, -1, 0, 0x140, -30, 0},      {0xD80, 1, 0x100, 0x100, 30, 0},
            {0xD58, -1, 0x200, 0, -30, -40},    {0xD58, 1, 0x300, 0x40, 30, -40},
            {0xD80, -1, 0x200, 0x140, -30, 0},  {0xD80, 1, 0x300, 0x100, 30, 0},
            {0xD58, -1, 0, 0x200, -30, -40},    {0xD58, 1, 0x100, 0x240, 30, -40},
            {0xD80, -1, 0x200, 0x340, -30, 0},  {0xD80, 1, 0x300, 0x300, 30, 0},
            {0xFFFF, -1, 0, 0x200, -30, -40},    {0xD58, 1, 0x300, 0x240, 30, -40},
            {0xD80, -1, 0x200, 0x340, -30, 0},  {0xD80, 1, 0x300, 0x300, 30, 0}};
        for (const G& g : kShower) {
            // entry 11 of the original uses x*2 instead of x+offset (an apparent bug in the original; kept)
            const int gx = g.st == 0xFFFF ? ex * 2 : ex + g.dx;
            gib(g.st == 0xFFFF ? 0xD58 : g.st, g.dir, gx, ey + g.dy, g.vx, g.vy);
        }
    } else if (kind == kKindZombie || kind == kKindWolf || kind == kKindDebris) {
        gib(0xD80, -1, ex, ey + 0x140, -30, 0);
        gib(0xD80, 1, ex + 0x100, ey + 0x100, 30, 0);
    }
    if (kind >= 0 && kind < 13) score_popup(ex + 0x40, ey + 0x40, dd2::ds_s16(uint16_t(0x1384 + 2 * kind)));   // tier by kind
    e.remove();
}

void World::collect(Enemy& e) {                                     // pickups (0x4582)
    const int type = e.pickup_type;
    int tier = 0;
    if (type >= 0 && type <= 4) { tier = type + 1; sound(16 + type); }   // TREASURE1..5 (0x4598..0x45D0)
    else if (type == 5) { ++lives_; sound(21); }                          // EXTRADAVE (0x45DD)
    score_popup(e.x(), e.y(), tier);                                // at the pickup itself (0x45E6; kills add 0x40)
    e.remove();
}

void World::player_contact(Enemy& e) {
    switch (e.kind) {
        case kKindZombie:                                           // only the third attack frame, facing Dave
            if (e.state().ds == 0xE48) {
                if (e.facing() == 1 && e.x() > player_.x()) return;
                if (e.facing() == -1 && e.x() < player_.x()) return;
                level_state_ = -3;
            }
            break;
        case kKindSlime: level_state_ = -2; break;
        case kKindSpider: level_state_ = -4; break;
        case kKindGhost:
            if (e.shootable) level_state_ = -5;                     // only once it is solid
            break;
        case kKindWolf: level_state_ = -6; break;
        case kKindSkull: level_state_ = -7; break;
        case kKindFrank: level_state_ = -7; break;
        case kKindSpark: level_state_ = -7; break;
        case kKindMasterClaw: level_state_ = -7; break;
        case kKindMasterFire: level_state_ = -7; break;
        case kKindKnife: level_state_ = -1; break;
        case kKindPickup: collect(e); break;
        default: break;                                             // other kinds are inert until ported
    }
    if (god && level_state_ < 0) level_state_ = 0;                  // god mode (0x45FA)
}

// Tile-touch scan (0x4729) over the tiles Dave overlaps: hazards kill, exit zones end the level, and door tiles with a
// layer-1 value record the trigger for the up-key handler.
void World::scan_touch() {
    door_ = DoorTouch{};
    const Box& b = player_.box();
    for (int col = b.lc; col <= b.rc; ++col)
        for (int row = b.tr; row <= b.br; ++row) {
            const int tile = tile_at(*level_, col, row);
            const int prop = dd2::tile_property(tile);
            if (prop == dd2::kPropHazard) { level_state_ = -8; }
            else if (prop == dd2::kPropExitZone) { level_state_ = 1; }
            else if (prop == 1 || prop == 2) {
                const uint16_t v = (col >= 0 && row >= 0 && col < level_->width && row < level_->height) ? level_->entity_at(col, row) : 0;
                if (v) door_ = DoorTouch{true, col, row, v, tile, prop};
            }
        }
}

// The three-row, two-column door art written over a door when it opens (table at DS:0x880).
void World::write_door_tiles(int col, int row, int anim) {
    auto kArt = [](int a, int i) { return dd2::ds_u16(uint16_t(0x880 + (a * 6 + i) * 2)); };   // DS:0x880
    for (int di = 0; di < 3; ++di)
        for (int si = 0; si < 2; ++si) {
            const int c = col - 1 + si, r = row - 1 + di;
            if (c < 0 || r < 0 || c >= level_copy_.width || r >= level_copy_.height) continue;
            level_copy_.tiles[size_t(r) * level_copy_.width + c] = kArt(anim, di * 2 + si);
        }
    ++tile_version_;
}

// Door sequences (think functions 0x3D62 open, 0x3C56 enter).
void World::handle_door_event() {
    const DoorTouch& d = door_;
    switch (player_.take_door_event()) {
        case DoorEvent::Open: {
            sound(12);                                              // OPENDOOR (0x3D6E)
            int anim = d.value > 0x8000 ? 0 : 2;
            if (d.tile == 0x27A) anim += 1;
            else if (d.tile == 0x2E4) anim += 2;
            if (d.value > 0x8000) {                                 // searchable spot: a pickup appears above it
                const int type = d.value - 0x8001;
                if (const dd2::SpawnDef* def = dd2::find_spawn_def(uint16_t(0x0E + type))) {
                    Enemy& e = spawn(kKindPickup, d.col << 8, (d.row - 1) << 8, -1, def->state_ptr);
                    e.pickup_type = type;
                    e.implemented = true;
                }
            }
            write_door_tiles(d.col, d.row, anim);
            break;
        }
        case DoorEvent::Enter: {
            if (d.value == 0x8000) { level_state_ = 1; break; }     // exit door
            const int dx = (d.value >> 8) & 0x7F, dy = d.value & 0x7F;
            player_.teleport(dx << 8, dy << 8, *level_);
            const int tile = tile_at(*level_, dx, dy);
            if (dd2::tile_property(tile) != 2)                       // the destination door opens as Dave steps out
                write_door_tiles(dx, dy, tile == 0x27A ? 3 : (tile == 0x2E4 ? 4 : 2));
            break;
        }
        default: break;
    }
}

void World::tick(const Input& in) {
    if (level_state_ != 0 && level_state_ != 100) return;           // dead / level over: frozen (100 = boss down, still running)
    ctx_.view_col = player_.camera().x >> 8;
    ctx_.view_row = player_.camera().y >> 8;
    const size_t n = objs_.size();

    // Phase 1: update (player first).
    player_.set_door(door_);
    player_.infinite_ammo = god;
    player_.begin_tick(in, *level_);
    handle_door_event();
    for (size_t i = 0; i < n; ++i) {
        Enemy& e = objs_[i];
        if (!e.kind || !e.implemented) continue;
        if (!e.active && e.wakes_up(ctx_.view_col, ctx_.view_row)) e.active = true;
        if (!e.active) continue;
        e.update(*level_);
        if (e.ended()) e.remove();
    }
    // Phase 2: overlaps and contact rules.
    for (size_t i = 0; i < n; ++i) {
        Enemy& e = objs_[i];
        if (!e.kind || !e.implemented || !e.active) continue;
        if (boxes_overlap(player_.box(), e.box())) {
            player_contact(e);
            if (e.kind == kKindGhost) e.touch_player();             // the ghost's own contact function
        }
    }
    // Phase 3: post-move (collision), in reverse order, player last.
    for (size_t i = n; i-- > 0;) {
        Enemy& e = objs_[i];
        if (!e.kind || !e.implemented || !e.active || e.state().post == 0) continue;
        e.post(*level_);
    }
    player_.end_tick(*level_);
    scan_touch();
    // (objects spawned during this tic start updating next tic, as in the original)

    if (level_state_ == 100) {                                      // the end-of-boss rule of the main loop (0x22EB..0x2344)
        level_state_ = 1;
        for (const Enemy& e : objs_)                                // anything but Delbert still active keeps the level open
            if (e.kind && e.active && e.kind != kKindDelbert) { level_state_ = 100; break; }
        if (level_state_ == 1 && level_number_ == 8) {              // level 8: free Delbert instead of ending
            level_state_ = 0;
            for (Enemy& e : objs_)
                if (e.kind == kKindDelbert) { e.force_state(0x1280); break; }
        }
    }
    objs_.erase(std::remove_if(objs_.begin(), objs_.end(), [](const Enemy& e) { return e.kind == 0; }), objs_.end());
}

}  // namespace ddhm
