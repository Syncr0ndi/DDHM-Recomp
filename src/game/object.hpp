// SPDX-License-Identifier: MIT
// object.hpp - the mechanics shared by every moving thing in the game (Dave, enemies, projectiles, debris),
// ported from the DOS executable's object update (0x1F2A), state setter (0x1EDE), box computation (0x1AE2),
// movement (0x310E) and collision resolver (0x1BA2 / 0x1C7D / 0x1D51).
#pragma once

#include <cstdint>

#include "dd2/dd2.hpp"

namespace ddhm {

// One 20-byte state record (DS:0x08BC..0x1384). think/contact/post are the original code addresses.
struct StateDef {
    uint16_t ds;
    int16_t sprite_w, sprite_e;
    int16_t flag;              // non-zero: "continuous" state, runs its think function every tic
    int16_t tics;              // duration before the state expires
    int16_t x_speed, y_speed;  // applied once per expiry, x scaled by facing and y by aim
    uint16_t think, contact, post;
    uint16_t next;             // ds address of the next state (0 = the object ends)
};
const StateDef* find_state(uint16_t ds);
inline const StateDef* find_player_state(uint16_t ds) { return find_state(ds); }

// Tile helpers (out-of-level cells read as tile 0).
int tile_at(const dd2::Level&, int col, int row);
bool tile_face(const dd2::Level&, int col, int row, dd2::TileFace);

struct Box {
    int l = 0, t = 0, r = 0, b = 0;           // pixel*16 bounds
    int lc = 0, tr = 0, rc = 0, br = 0;       // tile columns / rows they fall in
};
bool boxes_overlap(const Box& a, const Box& b);   // original 0x1A90 (inclusive)

class Object {
public:
    virtual ~Object() = default;

    int x() const { return x_; }
    int y() const { return y_; }
    int vx() const { return vx_; }
    int vy() const { return vy_; }
    int facing() const { return facing_; }
    int aim() const { return aim_; }
    int sprite() const { return sprite_; }
    bool on_ground() const { return on_ground_; }
    const StateDef& state() const { return *state_; }
    const Box& box() const { return box_; }
    bool ended() const { return ended_; }         // its state chain ran out (the object should be removed)

    // Top-left pixel where the current sprite is drawn.
    int draw_x() const;
    int draw_y() const;

    // One tic of the generic update (0x1F2A) and the state's post-move function.
    void update(const dd2::Level&);
    void post(const dd2::Level&);

protected:
    virtual void run_think(uint16_t think_addr, const dd2::Level&) = 0;
    virtual void run_post(uint16_t post_addr, const dd2::Level&) = 0;

    void set_state(uint16_t ds);
    void update_box();
    void apply_movement();
    void resolve_horizontal(const dd2::Level&);
    void resolve_vertical(const dd2::Level&);
    void resolve_both(const dd2::Level& lv) { resolve_horizontal(lv); resolve_vertical(lv); }
    void ground_probe(const dd2::Level&);

    int x_ = 0, y_ = 0;
    int dx_ = 0, dy_ = 0;                 // movement requested this tic (obj+0x2A / +0x2C)
    int vx_ = 0, vy_ = 0;                 // obj+0x2E / +0x30
    int facing_ = 1, aim_ = 0;
    const StateDef* state_ = nullptr;
    int elapsed_ = 0, delay_ = 0;
    int sprite_ = 0;
    Box box_, prev_;
    bool on_ground_ = false, hit_head_ = false, blocked_left_ = false, blocked_right_ = false;
    bool ended_ = false;
};

}  // namespace ddhm
