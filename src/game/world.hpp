// SPDX-License-Identifier: MIT
// world.hpp - one level of the game: the player, every other object, score and lives.
// Runs the original frame structure (main loop at 0x205F): update all objects in order, test overlaps and
// call contact rules, then post-move (collision) in reverse order. One call to tick() is one tic (1/70 s).
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "dd2/dd2.hpp"
#include "game/enemy.hpp"
#include "game/player.hpp"

namespace ddhm {

class World : public ShotTarget {
public:
    World();

    void new_game();                                   // score 0, 4 lives
    void load(const dd2::Level& level, int level_number = 0);   // (re)spawn the player and every enemy of the level
    void restart();                                    // after a death: lose a life and reload the level
    void tick(const Input&);

    Player& player() { return player_; }
    const Player& player() const { return player_; }
    const std::deque<Enemy>& objects() const { return objs_; }
    // The world works on its own copy of the level: opening doors rewrites tiles. tile_version() changes whenever
    // tiles change so a renderer knows to redraw.
    const dd2::Level& level() const { return *level_; }
    uint32_t tile_version() const { return tile_version_; }
    bool level_complete() const { return level_state_ == 1; }
    bool boss_down() const { return level_state_ == 100; }     // boss killed, waiting for the debris to settle
    int level_number() const { return level_number_; }

    uint32_t score() const { return score_; }
    int lives() const { return lives_; }
    void set_lives(int n) { lives_ = n; }               // the flow owns the lives counter between levels (DS:0x8D1E)
    void add_score_popup(int tier) { score_popup(player_.x(), player_.y(), tier); }   // F12+F debug bonus

    // Sounds requested since the last call (the original calls its sound routine 0x8FA8 directly).
    void sound(int n) { sounds_.push_back(n); }
    std::vector<int> take_sounds() { std::vector<int> v; v.swap(sounds_); return v; }

    bool god = false;
    // The original seeds its RNG index once at start-up and never resets it. Tests keep the old per-level reset.
    bool reset_rng_on_load = true;
    void seed_rng(int index) { rng_.seed(index); }                                   // DS:0x4BDE: no ammo use, contact deaths cancelled
    // Level state (DS:0x8D8E): 0 running, 1 exit, 100 boss killed, -1..-8 killed by something.
    int level_state() const { return level_state_; }
    bool dead() const { return level_state_ < 0; }

    void clear_objects() { objs_.clear(); }              // (tests/tools) remove every non-player object

    // Services used by enemies.
    Enemy* find_alive(uint32_t id, int kind);              // the live object with this id and kind, or nullptr
    void set_level_state(int s) { level_state_ = s; }
    Enemy& spawn(int kind, int x, int y, int dir, uint16_t state_ds);
    void kill(Enemy&);
    void score_popup(int x, int y, int tier);

    bool hit(int x, int y) override;                   // shot ray test (0x3F5E)

private:
    void spawn_level_objects();
    void player_contact(Enemy&);                       // the player's contact function (0x44EE)
    void collect(Enemy&);
    void scan_touch();                                 // tile-touch scan (0x4729): doors, hazards, exit zones
    void handle_door_event();
    void write_door_tiles(int col, int row, int anim);

    const dd2::Level* level_ = nullptr;                // points at level_copy_
    const dd2::Level* source_ = nullptr;               // the pristine level, for restarts
    dd2::Level level_copy_;
    DoorTouch door_;
    uint32_t tile_version_ = 0;
    Player player_;
    std::deque<Enemy> objs_;
    EnemyContext ctx_;
    Rng rng_;
    uint32_t score_ = 0, extra_life_base_ = 0;
    int lives_ = 4;
    int level_state_ = 0;
    int level_number_ = 0;
    uint32_t next_id_ = 1;
    std::vector<int> sounds_;
};

}  // namespace ddhm
