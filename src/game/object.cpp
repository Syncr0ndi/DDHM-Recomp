// SPDX-License-Identifier: MIT
#include "game/object.hpp"

#include <algorithm>

namespace ddhm {

// Object state records: DS:0x08BC..0x1384, 20 bytes each {sprite west, sprite east, continuous flag, tics, x speed,
// y speed, think, contact, post-move, next}, read from the user's DAVE.EXE (dd2::install_exe_tables).
const StateDef* find_state(uint16_t ds) {
    static std::vector<StateDef> cache;
    static uint32_t generation = 0;
    if (generation != dd2::exe_tables_generation()) {
        cache.clear();
        for (uint16_t a = 0x8BC; a < 0x1384; a = uint16_t(a + 20)) {
            StateDef s{};
            s.ds = a;
            s.sprite_w = dd2::ds_s16(a);
            s.sprite_e = dd2::ds_s16(uint16_t(a + 2));
            s.flag = dd2::ds_s16(uint16_t(a + 4));
            s.tics = dd2::ds_s16(uint16_t(a + 6));
            s.x_speed = dd2::ds_s16(uint16_t(a + 8));
            s.y_speed = dd2::ds_s16(uint16_t(a + 10));
            s.think = dd2::ds_u16(uint16_t(a + 12));
            s.contact = dd2::ds_u16(uint16_t(a + 14));
            s.post = dd2::ds_u16(uint16_t(a + 16));
            s.next = dd2::ds_u16(uint16_t(a + 18));
            cache.push_back(s);
        }
        generation = dd2::exe_tables_generation();
    }
    if (ds < 0x8BC || ds >= 0x1384 || (ds - 0x8BC) % 20) return nullptr;
    return &cache[size_t((ds - 0x8BC) / 20)];
}

int tile_at(const dd2::Level& lv, int col, int row) {
    if (col < 0 || row < 0 || col >= lv.width || row >= lv.height) return 0;
    return lv.tile_at(col, row);
}

bool tile_face(const dd2::Level& lv, int col, int row, dd2::TileFace f) {
    return dd2::tile_face_solid(tile_at(lv, col, row), f);
}

bool boxes_overlap(const Box& a, const Box& b) {
    return a.r >= b.l && a.b >= b.t && a.l <= b.r && a.t <= b.b;
}

int Object::draw_x() const { return (x_ + dd2::kSpriteTable[sprite_].draw_dx) >> 4; }
int Object::draw_y() const { return (y_ + dd2::kSpriteTable[sprite_].draw_dy) >> 4; }

void Object::set_state(uint16_t ds) {
    state_ = find_state(ds);
    sprite_ = facing_ > 0 ? state_->sprite_e : state_->sprite_w;
    if (state_->tics < elapsed_) elapsed_ = state_->tics - 1;        // original 0x1EDE
}

void Object::update_box() {                                           // original 0x1AE2
    const dd2::SpriteInfo& si = dd2::kSpriteTable[sprite_];
    box_.l = x_ + si.hit_left + si.draw_dx;
    box_.r = x_ + si.hit_right + si.draw_dx;
    box_.t = y_ + si.hit_top + si.draw_dy;
    box_.b = y_ + si.hit_bottom + si.draw_dy;
    box_.lc = box_.l >> 8;
    box_.rc = box_.r >> 8;
    box_.tr = box_.t >> 8;
    box_.br = box_.b >> 8;
}

void Object::apply_movement() {                                       // original 0x310E
    dx_ = std::clamp(dx_, -0x100, 0x100);
    dy_ = std::clamp(dy_, -0x100, 0x100);
    x_ += dx_;
    y_ += dy_;
    update_box();
}

// Original per-object update (0x1F2A), for a frame of exactly one tic.
void Object::update(const dd2::Level& lv) {
    on_ground_ = hit_head_ = blocked_left_ = blocked_right_ = false;
    elapsed_ += 1;
    if (state_->tics <= elapsed_) {
        elapsed_ -= state_->tics;
        update_box();
        prev_ = box_;
        dx_ = state_->x_speed * facing_;
        dy_ = state_->y_speed * aim_;
        if (state_->tics == 0) elapsed_ = 0;
        bool advance;
        if (state_->think == 0) {
            advance = true;
        } else if (delay_ > 0) {
            --delay_;
            advance = true;
        } else {
            const StateDef* old = state_;
            run_think(state_->think, lv);
            advance = (state_ == old);
        }
        if (advance) {
            if (state_->next) set_state(state_->next);
            else ended_ = true;
        }
        apply_movement();
    } else {
        if (state_->flag == 0) return;                                // nothing happens mid-state
        update_box();
        prev_ = box_;
        dx_ = dy_ = 0;
        if (state_->think) run_think(state_->think, lv);
        apply_movement();
    }
}

void Object::post(const dd2::Level& lv) { run_post(state_->post, lv); }

void Object::resolve_horizontal(const dd2::Level& lv) {               // original 0x1C7D
    bool done = false;
    for (int col = prev_.rc + 1; col <= box_.rc && !done; ++col)
        for (int row = box_.tr; row <= box_.br; ++row)
            if (tile_face(lv, col, row, dd2::TileFace::Left)) {
                const int shift = (box_.r + 1) - (col << 8);
                x_ -= shift; box_.l -= shift; box_.r -= shift;
                box_.rc = col - 1;
                blocked_right_ = true;
                done = true;
                break;
            }
    for (int col = prev_.lc - 1; col >= box_.lc; --col)
        for (int row = box_.tr; row <= box_.br; ++row)
            if (tile_face(lv, col, row, dd2::TileFace::Right)) {
                const int shift = ((col + 1) << 8) - box_.l;
                x_ += shift; box_.l += shift; box_.r += shift;
                box_.lc = col + 1;
                blocked_left_ = true;
                return;
            }
}

void Object::resolve_vertical(const dd2::Level& lv) {                 // original 0x1BA2
    bool done = false;
    for (int row = prev_.br + 1; row <= box_.br && !done; ++row)
        for (int col = box_.lc; col <= box_.rc; ++col)
            if (tile_face(lv, col, row, dd2::TileFace::Top)) {
                const int shift = (box_.b + 1) - (row << 8);
                y_ -= shift; box_.t -= shift; box_.b -= shift;
                box_.br = row - 1;
                on_ground_ = true;
                done = true;
                break;
            }
    for (int row = prev_.tr - 1; row >= box_.tr; --row)
        for (int col = box_.lc; col <= box_.rc; ++col)
            if (tile_face(lv, col, row, dd2::TileFace::Bottom)) {
                const int shift = ((row + 1) << 8) - box_.t;
                y_ += shift; box_.t += shift; box_.b += shift;
                box_.tr = row + 1;
                hit_head_ = true;
                return;
            }
}

void Object::ground_probe(const dd2::Level& lv) {                     // original 0x1D51
    if (!(box_.b & 1)) return;
    for (int col = box_.lc; col <= box_.rc; ++col)
        if (tile_face(lv, col, box_.br + 1, dd2::TileFace::Top)) { on_ground_ = true; return; }
}

}  // namespace ddhm
