// SPDX-License-Identifier: MIT
// flow.hpp - the game program around the play loop: title screen, level intro, death animations, game over,
// high score, "continue from level", the ending, the function-key windows and the debug keys. Ported routine by
// routine from DAVE.EXE (addresses in the comments of flow.cpp); runs on the Machine's program thread.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "dd2/dd2.hpp"
#include "dd2/exe.hpp"
#include "game/machine.hpp"
#include "game/world.hpp"

namespace ddhm {

// Everything loaded from the user's game folder.
struct Assets {
    dd2::ExeData exe;
    dd2::TileSet tiles;
    std::vector<dd2::Sprite> sprites;
    std::array<dd2::Level, 8> levels;
    dd2::Picture title1, title2, progpic;
};
Assets load_assets(const std::string& game_dir);     // throws dd2::Error with a readable message

// CTLPANEL.DD2 (read at 0x8D6F, written at 0x8E01): eight direction scan codes (N, NE, E, SE, S, SW, W, NW),
// button 1, button 2, high score. 14 bytes, the same file format as the DOS game.
struct ControlPanel {
    std::array<uint8_t, 8> dir{0x48, 0x49, 0x4D, 0x51, 0x50, 0x4F, 0x4B, 0x47};
    uint8_t button1 = sc::kCtrl, button2 = sc::kAlt;
    uint32_t high_score = 0;
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

struct GameOptions {
    std::string ctlpanel_path;                         // where CTLPANEL.DD2 is read from / written to ("" = nowhere)
    bool extra_keys = true;                            // Space/Z also jump, X also fires (port convenience)
    int rng_seed = -1;                                 // -1: from the clock like the original (0x9183), else fixed
    bool skip_title = false;                           // tests: start a game at `start_level` straight away
    int start_level = 1;
    std::function<void(const std::string&)> on_quit;   // called when the player confirms ESC -> Y
};

class Game {
public:
    Game(Machine& m, const Assets& a, GameOptions opt);
    void run();                                        // main loop of the program (0x1931: title forever)

    // State visible to tests.
    World world;
    ControlPanel panel;
    int level = 0;                                     // DS:0x9902
    bool in_game = false;                              // DS:0x535C

private:
    // Program structure.
    void title();                                      // 0x154E
    void play_game();                                  // 0x2364
    void level_intro();                                // 0x1201
    void wait_intro();                                 // 0x11D6
    void play_loop();                                  // 0x205F
    void death_animation();                            // 0x2449..0x24EA
    void ending();                                     // 0x10AA
    void high_score_and_continue(bool& again);         // 0x2552..0x271F

    // Screen helpers.
    void render_play();                                // the play screen (0x2797) into the canvas
    void fade_out();                                   // 0x8261
    void fade_in();                                    // 0x8208
    void set_palette_step(int step);
    void grow_window(int w, int h);                    // 0x84A3
    void grow_w(int w, int h);                         // 0x84FF
    void grow_h(int w, int h);                         // 0x8531
    void draw_sprite_px(int n, int x, int y);          // 0x8060: sprite at a pixel position (no draw offsets)
    std::string str(uint16_t ds) const { return a_.exe.ds_string(ds); }

    // Input.
    struct Control { int dir = 8; bool b1 = false, b2 = false; };
    Control read_control();                            // 0x7853 (keyboard 0x74AD, plus a gamepad if present)
    int get_key();                                     // 0x8AAE: blinking cursor, returns the ASCII code
    void wait_key();                                   // 0x78F9: new key press or a button
    bool check_keys();                                 // 0x041C: function keys and debug keys; true = handled
    void help_window();                                // 0x0239 (F1)
    void keyboard_config();                            // 0x7264 (F3)
    void joystick_config();                            // 0x6EBA (F4)
    void key_name(int scancode);                       // 0x70FD
    void status_window();                              // 0x055B (TAB)
    void save_panel();

    Machine& m_;
    Screen& s_;
    const Assets& a_;
    GameOptions opt_;

    bool reset_ = false;                               // DS:0x8D8A (F5 -> Y)
    bool show_tics_ = false;                           // DS:0x8D8C (F12+S)
    bool fade_in_pending_ = false;                     // DS:0x8D84
    bool continuing_ = false;                          // DS:0x4F20 used as "keep the level" on continue
    int intro_counter_ = 0, intro_frames_ = 0;         // DS:0x4BFC / 0x4F32
    bool pad_b1_jumps_ = true;                         // F4: "Should button 1 (A)ttack, or (J)ump ?"
};

}  // namespace ddhm
