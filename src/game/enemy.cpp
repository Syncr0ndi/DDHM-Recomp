// SPDX-License-Identifier: MIT
#include "game/enemy.hpp"

#include <algorithm>
#include <cstdlib>

#include "game/world.hpp"

namespace ddhm {

int Rng::next() {                                                    // 0x91A5: table at DS:0x3024 (from DAVE.EXE)
    idx_ = (idx_ + 1) & 255;
    return dd2::ds_u8(uint16_t(0x3024 + idx_));
}

void Enemy::init(int k, int px, int py, int dir, uint16_t state_ds) {
    kind = k;
    active = false;
    x_ = px;
    y_ = py;
    facing_ = dir;
    aim_ = 0;
    dx_ = dy_ = vx_ = vy_ = 0;
    elapsed_ = delay_ = 0;
    ended_ = false;
    set_state(state_ds);
    update_box();
    prev_ = box_;
}

bool Enemy::wakes_up(int vl, int vt) const {
    return vl - 2 < box_.rc && vt - 2 < box_.br && vl + 0x17 > box_.lc && vt + 0xC > box_.tr;
}

void Enemy::face_player() {                                           // think 0x3414
    facing_ = box_.l > ctx_->player->box().l ? -1 : 1;
}

void Enemy::tick_flash() {
    if (flash != 0) {
        flash -= 1;
        if (flash < 0) flash = 0;
    }
}

void Enemy::offscreen_check() {                                       // original 0x33C7
    const int vl = ctx_->view_col, vt = ctx_->view_row;
    const bool off = (vl - 8 > box_.rc) || (vt - 8 > box_.br) || (vl + 0x1C < box_.lc) || (vt + 0x12 < box_.tr);
    if (off) {
        if (kind >= 0xD) kind = 0;
        active = false;
    }
}

// Zombie AI (0x4831): face and attack Dave when close on the same level, drop through one-way floors toward him.
void Enemy::think_zombie(const dd2::Level& lv) {
    const Player& p = *ctx_->player;
    const Box& pb = p.box();
    const int dyabs = std::abs(box_.b - pb.b);
    int dist, dir;
    if (x_ > p.x()) { dist = box_.l - pb.r; dir = -1; }
    else { dist = pb.l - box_.r; dir = 1; }
    if (dyabs < 0x200) {
        facing_ = dir;
        if (dist <= 0x80) set_state(0xE20);                            // attack
    }
    const int rows = pb.br - box_.br;
    if (rows > 0 && rows < 10) {
        const int cols = std::abs(box_.lc - pb.lc);
        if (cols < 6) {
            for (int c = box_.lc; c <= box_.rc; ++c) {
                if (tile_face(lv, c, box_.br + 1, dd2::TileFace::Bottom)) return;
                if (!tile_face(lv, c, box_.br + 2, dd2::TileFace::Top)) return;
            }
            dy_ = 0x80;
            set_state(0xE5C);                                          // drop down
        }
    }
}

// Hunchback AI (0x49DB): keeps its distance, throws knives from afar.
void Enemy::think_hunchback() {
    const Player& p = *ctx_->player;
    const Box& pb = p.box();
    const int dyabs = std::abs(box_.b - pb.b);
    int dist, dir;
    if (x_ > p.x()) { dist = box_.l - pb.r; dir = -1; }
    else { dist = pb.l - box_.r; dir = 1; }
    if (dyabs <= 0x100) {
        if (dist > 0x700) {
            facing_ = dir;
            if (ctx_->rng->next() < 50) { set_state(0xED4); ctx_->world->sound(8); }   // wind up a throw (KNIFETHROW)
        } else if (dist < 0x300) {
            facing_ = -dir;                                            // too close: back away
        }
    } else if (ctx_->rng->next() < 2) {
        facing_ = -facing_;
    }
}

void Enemy::throw_knife() {                                           // think 0x4A72
    const int kx = facing_ > 0 ? x_ + 0x200 : x_;
    Enemy& k = ctx_->world->spawn(kKindKnife, kx, y_, facing_, 0xF10);
    k.shootable = false;
    k.implemented = true;
    k.active = true;
    delay_ = 4;
}

void Enemy::think_gib() {                                             // think 0x3972
    dx_ = vx_;
    vy_ = std::min(vy_ + 3, 80);
    dy_ += vy_;
    if (vy_ > 0 && aim_ <= 0) {
        set_state(0xD80);
        aim_ = 1;
    }
}

void Enemy::think_popup(int lifetime) {                               // think 0x380B / 0x3825
    timer += 1;
    if (timer > lifetime) remove();
}

// The Master (0x53DF): glides left and right, regrows his claws, and every 330 tics unleashes a fire attack.
void Enemy::think_master() {
    if (facing_ > 0) { if (x_ > 0xF00) facing_ = -1; }
    else if (x_ < 0x200) facing_ = 1;
    x_ += facing_ * 12;
    World& w = *ctx_->world;
    if (!w.find_alive(link_a, kKindMasterClaw)) {                    // left claw
        t54 += 1;
        if (t54 > 300) {
            t54 = 0;
            Enemy& cl = w.spawn(kKindMasterClaw, x_, y_ + 0x300, -1, 0x11F4);
            cl.shootable = cl.hurtable = cl.implemented = true;
            cl.hp = 2;
            cl.timer = 8;                                            // speed limit
            link_a = cl.id;
            set_state(0x11B8);
        }
    }
    if (!w.find_alive(link_b, kKindMasterClaw)) {                    // right claw
        t52 += 1;
        if (t52 > 300) {
            t54 = 0;                                                 // (sic) the original resets the LEFT timer here
            Enemy& cl = w.spawn(kKindMasterClaw, x_ + 0x400, y_ + 0x300, -1, 0x11F4);
            cl.shootable = cl.hurtable = cl.implemented = true;
            cl.hp = 2;
            cl.timer = 6;
            link_b = cl.id;
            set_state(0x11B8);
        }
    }
    t50 += 1;
    if (t50 > 330) {
        t50 = 0;
        t52 = 0;
        set_state(0x11CC);                                           // fire pose
        w.sound(23);                                                 // MASTFIRE (0x5500)
    }
}

// Fire pose (0x5506): after 60 tics four fireballs fly out and down.
void Enemy::think_master_fire() {
    t52 += 1;
    if (t52 <= 0x3C) return;
    struct F { int dx, dy, vx, vy; };
    static const F kFire[4] = {{0, 0x300, -30, 30}, {0x100, 0x400, -16, 40}, {0x300, 0x400, 16, 40}, {0x400, 0x300, 30, 30}};
    for (const F& f : kFire) {
        Enemy& b = ctx_->world->spawn(kKindMasterFire, x_ + f.dx, y_ + f.dy, -1, 0x1230);
        b.set_velocity(f.vx, f.vy);
        b.implemented = true;
        b.active = true;
    }
    set_state(0x1190);
}

// Delbert falling (0x56D9): lands, then hops.
void Enemy::post_delbert_fall(const dd2::Level& lv) {
    resolve_vertical(lv);
    if (on_ground_) set_state(0x1294);
}

// Frank (0x52A1): every 11th step he stops to throw a spark.
void Enemy::think_frank_step() {
    timer += 1;
    if (timer > 10) {
        timer -= 10;
        set_state(0x130C);
    }
}

// Frank's throw (0x52BB): a spark appears at his hand and hovers, then streaks off diagonally.
void Enemy::think_frank_throw() {
    int sx, dir;
    if (facing_ > 0) { sx = box_.r; dir = 1; }
    else { sx = box_.l - 0x100; dir = -1; }
    Enemy& s = ctx_->world->spawn(kKindSpark, sx, y_ + 0x100, dir, 0x1334);
    s.set_aim(1);
    s.hp = 1;
    s.shootable = s.hurtable = s.implemented = true;
    s.active = true;
}

void Enemy::think_spark_hover() {                                     // 0x532C
    age += 1;
    if (age >= 0x18) set_state(0x135C);
}

// Spark in flight (0x5342): ricochets off ceilings, floors and walls and dies after 13 bounces.
void Enemy::post_spark(const dd2::Level& lv) {
    resolve_both(lv);
    if (hit_head_) { aim_ = 1; timer += 1; }
    else if (on_ground_) { aim_ = -1; timer += 1; }
    if (blocked_left_) { facing_ = 1; timer += 1; }
    else if (blocked_right_) { facing_ = -1; timer += 1; }
    if (timer > 12) remove();
}

void Enemy::post_spark_gib(const dd2::Level& lv) {                    // 0x3A0E: any touch ends the chunk
    resolve_both(lv);
    if (on_ground_ || blocked_left_ || blocked_right_ || hit_head_) remove();
}

// Slime (0x4B7F): wanders, sometimes reverses, sometimes leaps between floor and ceiling.
void Enemy::think_slime() {
    if (ctx_->rng->next() < 0x20) facing_ = -facing_;
    if (ctx_->rng->next() < 0x10) {
        if (aim_ > 0) { aim_ = -1; set_state(0xF9C); }
        else { aim_ = 1; set_state(0xF74); }
    }
}

// Waiting ghost (0x4C76): materialises behind Dave, 2..5 tiles away, on about the same level.
void Enemy::think_ghost_wait() {
    const Player& p = *ctx_->player;
    if (std::abs(p.y() - y_) < 0x200) {
        const int dist = p.facing() > 0 ? p.x() - x_ : x_ - p.x();
        if (dist >= 0x200 && dist <= 0x500) {
            elapsed_ = 0;
            set_state(0x1000);                                         // fade in
            ctx_->world->sound(14);                                    // GHOSTFADE (0x4CC2)
        }
    }
}

// Homing flight of the ghost (0x4D16, +-20) and the flaming skull (0x501A, +-4, aims y_target_offset above itself).
void Enemy::think_homing(int max_speed, int y_target_offset) {
    const Player& p = *ctx_->player;
    const int dir_x = p.x() > x_ ? 1 : -1;
    const int dir_y = (y_target_offset == 0) ? (p.y() > y_ ? 1 : -1) : ((y_ + y_target_offset) >= p.y() ? -1 : 1);
    vx_ = std::clamp(vx_ + dir_x, -max_speed, max_speed);
    dx_ += vx_;
    vy_ = std::clamp(vy_ + dir_y, -max_speed, max_speed);
    dy_ += vy_;
    facing_ = vx_ > 0 ? 1 : -1;
}

// Wolf (0x4DE8): faces Dave on the same level and pounces when within 6 tiles.
void Enemy::think_wolf() {
    const Player& p = *ctx_->player;
    const Box& pb = p.box();
    const int dyabs = std::abs(box_.b - pb.b);
    int dist, dir;
    if (x_ > p.x()) { dist = box_.l - pb.r; dir = -1; }
    else { dist = pb.l - box_.r; dir = 1; }
    if (dyabs < 0x300) {
        facing_ = dir;
        if (dist < 0x600) {
            set_state(0x10F0);                                         // crouch, then leap
            vy_ = -65;
        }
    }
}

// Spider (0x510E): crawls on the ceiling following Dave; drops on its thread when he is right below.
void Enemy::think_spider() {
    const Player& p = *ctx_->player;
    const int below = p.y() - y_;
    if (below >= 0 && below < 0x600 && std::abs(p.x() - x_) < 0xA0) {
        aim_ = 1;
        set_state(0x1168);
        return;
    }
    if (p.x() > x_ && facing_ == -1) { facing_ = 1; set_state(state_->ds); }
    else if (p.x() < x_ && facing_ == 1) { facing_ = -1; set_state(state_->ds); }
}

void Enemy::run_think(uint16_t think, const dd2::Level& lv) {
    switch (think) {
        case 0x4831: think_zombie(lv); break;
        case 0x3414: face_player(); break;
        case 0x49DB: think_hunchback(); break;
        case 0x4A72: throw_knife(); break;
        case 0x3972: think_gib(); break;
        case 0x53DF: think_master(); break;
        case 0x5506: think_master_fire(); break;
        case 0x55D9: think_homing(timer, 0); break;                  // claw: homing, speed limit in obj+0x4C
        case 0x567E:                                                 // fireball: constant velocity
            dx_ = vx_;
            dy_ = vy_;
            break;
        case 0x56FF: ctx_->world->set_level_state(1); break;        // Delbert's celebration ends the level
        case 0x52A1: think_frank_step(); break;
        case 0x52BB: think_frank_throw(); break;
        case 0x532C: think_spark_hover(); break;
        case 0x530D:                                                // spark in flight: 24 units per tic along (facing, aim)
            dx_ = facing_ * 24;
            dy_ = aim_ * 24;
            break;
        case 0x399D:                                                // spark debris: like a gib without the frame change
            dx_ = vx_;
            vy_ = std::min(vy_ + 3, 80);
            dy_ += vy_;
            break;
        case 0x4B7F: think_slime(); break;
        case 0x4C76: think_ghost_wait(); break;
        case 0x4CD8: shootable = hurtable = true; ctx_->world->sound(15); break;   // ghost turns solid (GHOSTREAL)
        case 0x4D16: think_homing(20, 0); break;                    // ghost chases Dave
        case 0x501A: think_homing(4, -0xC0); break;                 // flaming skull
        case 0x4DE8: think_wolf(); break;
        case 0x4F33:                                                // wolf leap: gravity
            vy_ = std::min(vy_ + 3, 80);
            dy_ += vy_;
            break;
        case 0x510E: think_spider(); break;
        case 0x380B: think_popup(60); break;
        case 0x3825: think_popup(180); break;
        default: break;                                                // other enemies: not ported yet
    }
}

// Ground walker post-move (0x3218): turn at walls and ledges, with a random pause of its AI.
void Enemy::post_walker(const dd2::Level& lv) {
    resolve_horizontal(lv);
    const int col = facing_ > 0 ? box_.rc : box_.lc;
    const bool floor = tile_face(lv, col, box_.br + 1, dd2::TileFace::Top);
    if (!floor || (blocked_left_ && facing_ == -1) || (blocked_right_ && facing_ == 1)) {
        if (delay_ != 0) delay_ = 0;
        else delay_ = ctx_->rng->next() / 32;
        facing_ = -facing_;
        prev_ = box_;
        set_state(state_->ds);
        update_box();
        resolve_horizontal(lv);
    }
    tick_flash();
    offscreen_check();
}

void Enemy::post_zombie_down(const dd2::Level& lv) {                  // 0x492A
    if (tile_face(lv, box_.lc, box_.br, dd2::TileFace::Bottom) || box_.br > ctx_->player->box().br) {
        resolve_vertical(lv);
        set_state(0xDD0);
    }
    tick_flash();
}

void Enemy::post_knife(const dd2::Level& lv) {                        // 0x4ABD
    resolve_horizontal(lv);
    if (blocked_left_ || blocked_right_) set_state(0xBDC);
    offscreen_check();
}

void Enemy::post_gib(const dd2::Level& lv) {                          // 0x39AB
    resolve_both(lv);
    if (on_ground_) {
        y_ += 0x80;
        set_state(0xDA8);
    } else if (blocked_left_ || blocked_right_) {
        facing_ = -facing_;
        vx_ = (-vx_) / 2;
        set_state(state_->ds);
    }
    if (hit_head_) vy_ = 0;
}

void Enemy::turn_around(const dd2::Level& lv) {
    facing_ = -facing_;
    prev_ = box_;
    set_state(state_->ds);
    update_box();
    resolve_horizontal(lv);
}

// Ceiling walker post-move (0x32FD): like the ground walker but follows a ceiling.
void Enemy::post_ceiling_walker(const dd2::Level& lv) {
    resolve_both(lv);
    const int col = facing_ > 0 ? box_.rc : box_.lc;
    const bool ceiling = tile_face(lv, col, box_.br - 1, dd2::TileFace::Bottom);
    if (!ceiling || (blocked_left_ && facing_ == -1) || (blocked_right_ && facing_ == 1)) turn_around(lv);
    tick_flash();
    offscreen_check();
}

// Slime leap (0x4BB9): arrives on the ceiling / floor and settles.
void Enemy::post_slime_jump(const dd2::Level& lv) {
    resolve_both(lv);
    if (aim_ == -1 && hit_head_) set_state(0xFC4);
    else if (aim_ == 1 && on_ground_) { set_state(0xFD8); ctx_->world->sound(2); }   // SLIMESLOP (0x4BEA)
    offscreen_check();
}

// Wolf running (0x4E52): turns at walls, leaps across gaps.
void Enemy::post_wolf_run(const dd2::Level& lv) {
    resolve_horizontal(lv);
    const int col = facing_ > 0 ? box_.rc : box_.lc;
    const bool floor = tile_face(lv, col, box_.br + 1, dd2::TileFace::Top);
    const bool wall = (blocked_left_ && facing_ == -1) || (blocked_right_ && facing_ == 1);
    if (wall) {
        turn_around(lv);
        delay_ = 0;
    } else if (!floor) {
        set_state(0x10F0);
        vy_ = -65;
        delay_ = 0;
    }
    tick_flash();
    offscreen_check();
}

// Wolf in the air (0x4F37): lands, pauses its AI for a few steps, turns if it hit a wall.
void Enemy::post_wolf_jump(const dd2::Level& lv) {
    resolve_both(lv);
    if (blocked_left_ || blocked_right_) { delay_ = 0; bumped_ = true; }
    if (on_ground_) {
        delay_ = 7;
        if (bumped_) { facing_ = -facing_; delay_ = 0; bumped_ = false; }
        set_state(0x10A0);
        prev_ = box_;
        set_state(0x10A0);
        update_box();
        resolve_both(lv);
    }
    tick_flash();
    offscreen_check();
}

// Spider dropping on its thread (0x517A): stops on a solid ceiling-face tile or at full length, then rises.
void Enemy::post_spider_drop(const dd2::Level& lv) {
    resolve_horizontal(lv);
    for (int c = box_.lc; c <= box_.rc; ++c)
        if (tile_face(lv, c, box_.br, dd2::TileFace::Bottom)) { aim_ = -1; set_state(0x117C); break; }
    if (((y_ - timer) & 0xFFFF) > 0x600) { aim_ = -1; set_state(0x117C); }
}

void Enemy::post_spider_rise(const dd2::Level& lv) {                  // 0x5211
    resolve_horizontal(lv);
    if (y_ <= timer) {
        y_ = timer;
        set_state(0x1140);
    }
}

void Enemy::touch_player() {                                          // ghost contact (0x4CEB): turn solid
    if (state_->contact == 0x4CEB) {
        state_ = find_state(0x103C);
        sprite_ = state_->sprite_e;
    }
}

void Enemy::extra_draws(std::vector<ExtraDraw>& out) const {
    if (kind != kKindSpider || (state_->ds != 0x1168 && state_->ds != 0x117C)) return;
    for (int yy = timer; y_ + 0x80 > yy; yy += 0x80) out.push_back({154, x_ + 0x40, yy});      // SPIDERSILK
    out.push_back({153, x_, y_});                                                              // SPIDERDOWN
}

void Enemy::run_post(uint16_t post, const dd2::Level& lv) {
    switch (post) {
        case 0x3218: post_walker(lv); break;
        case 0x492A: post_zombie_down(lv); break;
        case 0x4ABD: post_knife(lv); break;
        case 0x39AB: post_gib(lv); break;
        case 0x315C: offscreen_check(); break;                          // plain commit (pickups, popups, flashes)
        case 0x3172: tick_flash(); offscreen_check(); break;
        case 0x31B7: resolve_both(lv); offscreen_check(); break;
        case 0x31D0: resolve_both(lv); tick_flash(); offscreen_check(); break;
        case 0x32FD: post_ceiling_walker(lv); break;
        case 0x4BB9: post_slime_jump(lv); break;
        case 0x56D9: post_delbert_fall(lv); break;
        case 0x56C6: break;                                          // plain commit (no off-screen check)
        case 0x5342: post_spark(lv); break;
        case 0x3A0E: post_spark_gib(lv); break;
        case 0x4E52: post_wolf_run(lv); break;
        case 0x4F37: post_wolf_jump(lv); break;
        case 0x50B8: resolve_both(lv); offscreen_check(); break;
        case 0x517A: post_spider_drop(lv); break;
        case 0x5211: post_spider_rise(lv); break;
        default: break;
    }
}

}  // namespace ddhm
