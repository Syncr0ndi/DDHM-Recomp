// SPDX-License-Identifier: MIT
// player.hpp - Dave's movement, jumping, aiming and shooting, ported from DAVE.EXE.
//
// Everything here is a direct port of routines found in the DOS executable (see docs/PLAYER.md for the
// addresses). The simulation advances in "tics" of 1/70 s, exactly like the original's frame timer, and
// uses its fixed-point units: 256 units per tile, 16 units per pixel. No platform dependencies.
#pragma once

#include <cstdint>
#include <vector>

#include "dd2/dd2.hpp"
#include "game/object.hpp"

namespace ddhm {

constexpr int kTicsPerSecond = 70;

// Direction codes used by the original input routine: clockwise from up, 8 = neutral.
enum Dir { kDirUp = 0, kDirUpRight, kDirRight, kDirDownRight, kDirDown, kDirDownLeft, kDirLeft, kDirUpLeft, kDirNone };

struct Input {
    int dir = kDirNone;
    bool jump = false;
    bool fire = false;
};
Input make_input(bool up, bool down, bool left, bool right, bool jump, bool fire);

// Short-lived visual objects: muzzle flash, smoke puffs and the wall-impact flash.
struct Effect {
    const StateDef* state = nullptr;
    int x = 0, y = 0;          // object origin, 1/256 tile
    int facing = 1;
    int elapsed = 0;
};

struct Camera { int x = 0, y = 0; };   // top-left of the 320x200 view, 1/256 tile (provisional rules)
void update_camera(Camera&, int player_x, int player_y, const dd2::Level&);
void init_camera(Camera&, int player_x, int player_y, const dd2::Level&);

// The door / trigger tile under Dave, found by the tile-touch scan (0x4729) every frame.
struct DoorTouch {
    bool valid = false;
    int col = 0, row = 0;       // tile that carries the layer-1 value
    uint16_t value = 0;         // layer-1 value: destination (x<<8)|y, 0x8000 exit, 0x8001+ hidden pickup
    int tile = 0;               // tile id
    int prop = 0;               // tile property: 1 = closed door, 2 = open door
};
enum class DoorEvent { None, Open, Enter };

// Implemented by the world so a shot ray can hit enemies (original ray loop at 0x3F5E).
struct ShotTarget {
    virtual ~ShotTarget() = default;
    virtual bool hit(int x, int y) = 0;     // true when the point is inside a shootable enemy (damage is applied)
};

class Player : public Object {
public:
    Player();
    void spawn(int tile_x, int tile_y, const dd2::Level&);   // like the original 0xFF handler (0x3A48)

    // A full tic, or the two halves the world needs so enemies can run in between.
    void tick(const Input&, const dd2::Level&);
    void begin_tick(const Input&, const dd2::Level&);      // input + object update
    void end_tick(const dd2::Level&);                      // post-move, effects, camera

    void set_shot_target(ShotTarget* t) { targets_ = t; }
    void set_sound_sink(std::vector<int>* s) { sounds_ = s; }   // sound numbers requested this tic (0x8FA8 calls)
    bool infinite_ammo = false;                                   // god mode (DS:0x4BDE) skips the ammo decrement
    void set_door(const DoorTouch& d) { door_ = d; }
    DoorEvent take_door_event() { const DoorEvent e = door_event_; door_event_ = DoorEvent::None; return e; }
    void teleport(int x, int y, const dd2::Level&);          // end of the door-enter sequence (0x3C56)

    int ammo() const { return ammo_; }
    void add_ammo_full() { ammo_ = 8; }
    const std::vector<Effect>& effects() const { return effects_; }
    const Camera& camera() const { return cam_; }

protected:
    void run_think(uint16_t think, const dd2::Level&) override;
    void run_post(uint16_t post, const dd2::Level&) override;

private:
    void think_stand_walk();
    void think_air();
    void think_aim_hold(const dd2::Level&);
    void think_fire(const dd2::Level&);
    void think_reload();
    void dir_change();

    int cast_ray(const dd2::Level&, int x, int y, int dx, int dy, int& impact_x, int& impact_y) const;
    void spawn_effect(uint16_t ds, int x, int y, int facing);
    void update_effects();

    Input in_;
    int prev_dir_ = kDirNone;
    bool jump_latched_ = false, fire_ready_ = false;
    int jump_timer_ = 0;                  // DS:0x91D0, rise ticks remaining
    int ammo_ = 8;
    int idle_ = 0;                        // DS:0x91CE, idle tics before the reload animation
    Camera cam_;
    std::vector<Effect> effects_;
    ShotTarget* targets_ = nullptr;
    std::vector<int>* sounds_ = nullptr;
    void sound(int n) { if (sounds_) sounds_->push_back(n); }
    DoorTouch door_;
    DoorEvent door_event_ = DoorEvent::None;
};

}  // namespace ddhm
