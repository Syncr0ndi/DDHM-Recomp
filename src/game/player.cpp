// SPDX-License-Identifier: MIT
#include "game/player.hpp"

#include <algorithm>

namespace ddhm {

namespace {

// State addresses (DS offsets) used by the logic.
enum : uint16_t {
    kStand = 0x8bc, kWalk1 = 0x8d0,
    kJump1 = 0x9c0, kJump2 = 0x9d4, kJump3 = 0x9e8, kJump3b = 0x9fc,
    kAim = 0xa10, kAimFire2 = 0xa24,
    kAimNHold = 0xa4c, kAimNFire = 0xa60, kAimNFire2 = 0xa74,
    kAimSHold = 0xa9c, kAimSFire = 0xab0, kAimSFire2 = 0xac4,
    kOpen = 0x970, kEnter1 = 0x984,
    kReload1 = 0xaec,
    kFlashN = 0xb28, kFlashE = 0xb64, kFlashS = 0xba0, kHitFlash = 0xbdc,
};
// Original code addresses used as identifiers for think / post-move functions.
enum : uint16_t {
    kThinkStandWalk = 0x3b98, kThinkAir = 0x437e, kThinkFire = 0x40e7, kThinkAimHold = 0x3e76,
    kThinkReload = 0x3f34, kThinkAimReturn = 0x437a, kThinkDoorOpen = 0x3d62, kThinkDoorEnter = 0x3c56,
    kPostGround = 0x463f, kPostAim = 0x4687, kPostAir = 0x46b0, kPostReload = 0x31d0,
};

}  // namespace

Input make_input(bool up, bool down, bool left, bool right, bool jump, bool fire) {
    const int dx = (right ? 1 : 0) - (left ? 1 : 0);
    const int dy = (down ? 1 : 0) - (up ? 1 : 0);
    static const int table[3][3] = {   // [dy+1][dx+1]
        {kDirUpLeft, kDirUp, kDirUpRight},
        {kDirLeft, kDirNone, kDirRight},
        {kDirDownLeft, kDirDown, kDirDownRight}};
    Input in;
    in.dir = table[dy + 1][dx + 1];
    in.jump = jump;
    in.fire = fire;
    return in;
}

// Camera (0x1D90). Limits come from the level loader (0xEB2..0xED6): the view may not come closer than 2 tiles to the
// top/left edge, and stops at (width-22) x (height-15) tiles, which keeps the border tiles out of sight.
static void camera_limits(const dd2::Level& lv, int& min_x, int& min_y, int& max_x, int& max_y) {
    min_x = min_y = 0x200;
    max_x = (lv.width - 22) * 256;
    max_y = (lv.height - 15) * 256;
}

void update_camera(Camera& c, int px, int py, const dd2::Level& lv) {
    int min_x, min_y, max_x, max_y;
    camera_limits(lv, min_x, min_y, max_x, max_y);
    if (c.x + 0x900 > px) {                                           // player left of the dead zone
        const int d = c.x + 0x900 - px;
        c.x = (c.x - min_x < d) ? min_x : c.x - d;
    } else if (c.x + 0xB00 < px) {                                    // right of it
        const int d = px - (c.x + 0xB00);
        c.x = (d + c.x > max_x) ? max_x : c.x + d;
    }
    if (c.y + 0x400 > py) {
        const int d = c.y + 0x400 - py;
        c.y = (c.y - min_y < d) ? min_y : c.y - d;
    } else if (c.y + 0x800 < py) {
        const int d = py - (c.y + 0x800);
        c.y = (d + c.y > max_y) ? max_y : c.y + d;
    }
}

// Initial camera when a level starts (0x1E64).
void init_camera(Camera& c, int px, int py, const dd2::Level& lv) {
    int min_x, min_y, max_x, max_y;
    camera_limits(lv, min_x, min_y, max_x, max_y);
    c.x = px - 0xA00;
    c.y = py - 0x700;
    if (c.x < min_x || px < 0xA00) c.x = min_x;
    if (c.y < min_y || py < 0x700) c.y = min_y;
    if (c.x > max_x) c.x = max_x;
    if (c.y > max_y) c.y = max_y;
}

Player::Player() {
    state_ = find_player_state(kStand);
    sprite_ = state_->sprite_e;
}


void Player::spawn(int tile_x, int tile_y, const dd2::Level& lv) {
    x_ = tile_x << 8;
    y_ = (tile_y << 8) + 0xF;
    dx_ = dy_ = vx_ = vy_ = 0;
    facing_ = 1;
    aim_ = 0;
    elapsed_ = delay_ = 0;
    on_ground_ = hit_head_ = blocked_left_ = blocked_right_ = false;
    prev_dir_ = kDirNone;
    jump_latched_ = false;
    fire_ready_ = false;
    jump_timer_ = 0;
    ammo_ = 8;
    idle_ = 0;
    effects_.clear();
    set_state(kStand);
    update_box();
    prev_ = box_;
    init_camera(cam_, x_, y_, lv);
}

void Player::teleport(int x, int y, const dd2::Level& lv) {
    x_ = x;
    y_ = y;
    update_box();
    prev_ = box_;
    init_camera(cam_, x_, y_, lv);
}

void Player::begin_tick(const Input& in, const dd2::Level& lv) {
    in_ = in;
    if (!in_.fire) fire_ready_ = true;
    update(lv);
}

void Player::end_tick(const dd2::Level& lv) {
    post(lv);
    update_effects();
    update_camera(cam_, x_, y_, lv);
}

void Player::tick(const Input& in, const dd2::Level& lv) {
    begin_tick(in, lv);
    end_tick(lv);
}

void Player::run_think(uint16_t think, const dd2::Level& lv) {
    switch (think) {
        case kThinkStandWalk: think_stand_walk(); break;
        case kThinkAir: think_air(); break;
        case kThinkFire: think_fire(lv); break;
        case kThinkAimHold: think_aim_hold(lv); break;
        case kThinkReload: think_reload(); break;
        case kThinkAimReturn: dir_change(); break;
        case kThinkDoorOpen: door_event_ = DoorEvent::Open; break;       // the world opens the door / spawns the item
        case kThinkDoorEnter: door_event_ = DoorEvent::Enter; break;     // the world teleports Dave / ends the level
        default: break;
    }
}

// Original direction handler (0x3AB5).
void Player::dir_change() {
    switch (in_.dir) {
        case kDirUp:
            if (door_.valid) {                                                // 0x3AC8: a door tile is under Dave
                if (door_.prop == 1) { x_ = door_.col << 8; set_state(kOpen); }
                else { x_ = ((door_.col - 1) << 8) + 0x40; set_state(kEnter1); }
            } else {
                aim_ = -1;
                set_state(kAimNHold);
            }
            break;
        case kDirUpRight:   facing_ = 1;  aim_ = -1; set_state(kAimNHold); break;
        case kDirRight:     facing_ = 1;  set_state(kWalk1); break;
        case kDirDownRight: facing_ = 1;  aim_ = 1; set_state(kAimSHold); break;
        case kDirDown:      aim_ = 1; set_state(kAimSHold); break;
        case kDirDownLeft:  facing_ = -1; aim_ = 1; set_state(kAimSHold); break;
        case kDirLeft:      facing_ = -1; set_state(kWalk1); break;
        case kDirUpLeft:    facing_ = -1; aim_ = -1; set_state(kAimNHold); break;
        case kDirNone:      set_state(kStand); break;
        default: break;
    }
    prev_dir_ = in_.dir;
}

// Standing / walking think (0x3B98).
void Player::think_stand_walk() {
    if (in_.jump) {
        if (!jump_latched_) {
            jump_latched_ = true;
            jump_timer_ = 14;
            vx_ = (in_.dir == kDirLeft || in_.dir == kDirRight) ? facing_ * 24 : 0;
            vy_ = -40;
            set_state(kJump1);
            sound(3);                                                 // JUMP (0x3BE6)
            return;
        }
    } else {
        jump_latched_ = false;
    }
    if (in_.fire && fire_ready_) {
        fire_ready_ = false;
        aim_ = 0;
        set_state(kAim);
        return;
    }
    if (in_.dir != prev_dir_) dir_change();
    if (state_->ds == kStand && ammo_ < 8) {
        idle_ += 1;
        if (idle_ > 30) set_state(kReload1);
    } else {
        idle_ = 0;
    }
}

// Jump / fall think (0x437E).
void Player::think_air() {
    const bool right = in_.dir >= kDirUpRight && in_.dir <= kDirDownRight;
    const bool left = in_.dir >= kDirDownLeft && in_.dir <= kDirUpLeft;
    if (right) {
        vx_ = std::min(vx_ + 2, 24);
        dx_ += vx_;
        if (dx_ > 0) { facing_ = 1; sprite_ = state_->sprite_e; }
    } else if (left) {
        vx_ = std::max(vx_ - 2, -24);
        dx_ += vx_;
        if (dx_ < 0) { facing_ = -1; sprite_ = state_->sprite_w; }
    } else if (dx_ > 0) {
        vx_ -= 1; dx_ += vx_;
        if (vx_ < 0) vx_ = 0;
    } else if (dx_ < 0) {
        vx_ += 1; dx_ += vx_;
        if (vx_ < 0) vx_ = 0;
    }

    if (jump_timer_ != 0) {                                           // rising while the button is held
        dy_ = vy_;
        jump_timer_ -= 1;
        if (!in_.jump) jump_timer_ = 0;
        if (jump_timer_ == 0) set_state(kJump2);
    } else {                                                          // gravity (0x1B74)
        vy_ = std::min(vy_ + 3, 80);
        dy_ += vy_;
        if (state_->ds != kJump3 && state_->ds != kJump3b && vy_ > 0) set_state(kJump3);
    }
}

// Aim-up / aim-down hold think (0x3E76).
void Player::think_aim_hold(const dd2::Level& lv) {
    if (in_.jump) {
        if (aim_ < 0) {                                               // jump out of the aim-up pose
            jump_timer_ = 14;
            vx_ = 0;
            vy_ = -40;
            set_state(kJump1);
            sound(3);                                                 // JUMP (0x3EA4)
            return;
        }
        // Aiming down + jump: drop through a one-way floor, unless the tiles below are fully solid.
        for (int c = box_.lc; c <= box_.rc; ++c)
            if (tile_face(lv, c, box_.br + 1, dd2::TileFace::Bottom)) return;
        y_ += 0x40;
        prev_.br += 1;
        set_state(kJump3);
        sound(3);                                                     // JUMP (0x3EEE)
    } else if (in_.fire && fire_ready_) {
        fire_ready_ = false;
        set_state(aim_ < 0 ? kAimNFire : kAimSFire);
    } else if (in_.dir != prev_dir_) {
        dir_change();
    }
}

// Reload think (0x3F34): one shell per cycle, stand up when full.
void Player::think_reload() {
    sound(13);                                                        // RELOAD (0x3F38)
    ammo_ += 1;
    if (ammo_ == 8) set_state(kStand);
}

// Hitscan ray (0x3F53). Returns -1 when it ends on a wall, 0 when it leaves the screen range.
// (Enemy hits will be added with the enemy objects.)
int Player::cast_ray(const dd2::Level& lv, int x, int y, int dx, int dy, int& ix, int& iy) const {
    const int cam_col = cam_.x >> 8, cam_row = cam_.y >> 8;
    for (int guard = 0; guard < 8192; ++guard) {
        if (targets_ && targets_->hit(x, y)) { ix = x; iy = y; return 1; }
        const int ncol = (x + dx) >> 8;
        const int nrow = (y + dy) >> 8;
        if ((x >> 8) != ncol) {
            const int t = tile_at(lv, ncol, nrow);
            if (dx > 0) {
                if (ncol - cam_col > 0x19) return 0;
                if (dd2::tile_face_solid(t, dd2::TileFace::Left)) { x += 0xF - (x & 0xF); ix = x; iy = y; return -1; }
            } else {
                if (cam_col - ncol > 5) return 0;
                if (dd2::tile_face_solid(t, dd2::TileFace::Right)) { x -= (x & 0xF); ix = x; iy = y; return -1; }
            }
        }
        x += dx;
        if ((y >> 8) != nrow) {
            const int t = tile_at(lv, ncol, nrow);
            if (dy > 0) {
                if (nrow - cam_row > 0xE) return 0;
                if (dd2::tile_face_solid(t, dd2::TileFace::Top)) { y += 0xF - (y & 0xF); ix = x; iy = y; return -1; }
            } else {
                if (cam_row - nrow > 3) return 0;
                if (dd2::tile_face_solid(t, dd2::TileFace::Bottom)) { y -= (y & 0xF); ix = x; iy = y; return -1; }
            }
        }
        y += dy;
    }
    return 0;
}

// Fire think (0x40E7): runs when the wind-up state expires.
void Player::think_fire(const dd2::Level& lv) {
    if (ammo_ == 0) {                                                 // empty: click, step back, stand
        x_ += facing_ * 64;
        set_state(kStand);
        sound(7);                                                     // NOAMMO (0x410A)
        return;
    }
    switch (in_.dir) {                                                // re-aim from the held direction
        case kDirUp:        aim_ = -1; break;
        case kDirUpRight:   facing_ = 1;  aim_ = -1; break;
        case kDirRight:     aim_ = 0; facing_ = 1; break;
        case kDirDownRight: facing_ = 1;  aim_ = 1; break;
        case kDirDown:      aim_ = 1; break;
        case kDirDownLeft:  facing_ = -1; aim_ = 1; break;
        case kDirLeft:      aim_ = 0; facing_ = -1; break;
        case kDirUpLeft:    facing_ = -1; aim_ = -1; break;
        default: break;
    }
    if (aim_ == -1) { if (state_->ds != kAimNFire) set_state(kAimNFire2); }
    else if (aim_ == 0) { if (state_->ds != kAim) set_state(kAimFire2); }
    else if (aim_ == 1) { if (state_->ds != kAimSFire) set_state(kAimSFire2); }
    if (!infinite_ammo) ammo_ -= 1;

    uint16_t flash;
    int first = 0, last = 0, my, mx;
    if (aim_ == -1) {
        flash = kFlashN; first = 0; last = 2;
        my = y_ + 0x10; mx = x_ + (facing_ > 0 ? 0x120 : 0x30);
    } else if (aim_ == 0) {
        flash = kFlashE;
        my = y_ + 0xA0; mx = x_ + (facing_ > 0 ? 0x140 : 0xA0);
    } else {
        flash = kFlashS; first = 3; last = 7;
        my = y_ + 0x150; mx = x_ + (facing_ > 0 ? 0x120 : 0x30);
    }
    spawn_effect(flash, x_, y_, facing_);

    auto kRayDy = [](int i) { return int(dd2::ds_s16(uint16_t(0x13B8 + 2 * i))); };   // ray dy table (dx is always 128)
    int ix = 0, iy = 0, hit = 0;
    int result = 0;
    if (aim_ == 0) {
        result = cast_ray(lv, mx, my, facing_ << 7, 0, ix, iy);
        if (result == 0) result = cast_ray(lv, mx, my + 0x40, facing_ << 7, 0, ix, iy);
        if (result == 0) result = cast_ray(lv, mx, my - 0x80, facing_ << 7, 0, ix, iy);
        if (result != 0) hit = 1;
    } else {
        for (int si = first; si <= last; ++si) {
            result = cast_ray(lv, mx, my, 128 * facing_, kRayDy(si), ix, iy);
            if (result != 0) hit = 1;
            if (result == 1) break;
        }
    }
    if (hit) spawn_effect(kHitFlash, ix - 0x40, iy - 0x40, 0);
    sound(hit ? 5 : 6);                                               // impact / miss (0x4355..0x435E)
}

// Post-move functions selected by the state's third function pointer.
void Player::run_post(uint16_t post_addr, const dd2::Level& lv) {
    switch (post_addr) {
        case kPostGround:                                             // 0x463F
            resolve_horizontal(lv);
            ground_probe(lv);
            if ((blocked_left_ || blocked_right_) && state_->ds != kWalk1) set_state(kStand);
            if (!on_ground_) set_state(kJump3);
            break;
        case kPostAim:                                                // 0x4687
            resolve_horizontal(lv);
            ground_probe(lv);
            if (!on_ground_) set_state(kJump3);
            break;
        case kPostAir:                                                // 0x46B0
            resolve_both(lv);
            if (blocked_left_ || blocked_right_) vx_ = 0;
            if (on_ground_) {                                         // landed: pick stand/walk, nudge forward
                sound(4);                                             // LAND (0x46D5)
                dir_change();
                x_ += facing_ * 64;
                update_box();
                resolve_both(lv);
            } else if (hit_head_) {                                   // bumped the ceiling
                sound(24);                                            // HITHEAD (0x46FE)
                vy_ = 0;
                set_state(kJump3);
            }
            break;
        case kPostReload:                                             // 0x31D0
            resolve_both(lv);
            break;
        default: break;
    }
}

void Player::spawn_effect(uint16_t ds, int x, int y, int facing) {
    Effect e;
    e.state = find_player_state(ds);
    e.x = x; e.y = y; e.facing = facing;
    effects_.push_back(e);
}

// Effects use the same state timing as every object: when `tics` expire they move by speed (smoke rises:
// aim is -1) and advance to the next state; with no next state they disappear.
void Player::update_effects() {
    for (size_t i = 0; i < effects_.size();) {
        Effect& e = effects_[i];
        e.elapsed += 1;
        bool dead = false;
        if (e.state->tics <= e.elapsed) {
            e.elapsed -= e.state->tics;
            e.x += e.state->x_speed * e.facing;
            e.y += e.state->y_speed * -1;
            if (e.state->next) e.state = find_player_state(e.state->next); else dead = true;
        }
        if (dead) effects_.erase(effects_.begin() + long(i)); else ++i;
    }
}

}  // namespace ddhm
