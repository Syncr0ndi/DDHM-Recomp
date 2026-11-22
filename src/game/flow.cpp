// SPDX-License-Identifier: MIT
#include "game/flow.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace ddhm {

// ---------------------------------------------------------------------------------------------------------------
// Assets and the control panel file
// ---------------------------------------------------------------------------------------------------------------

Assets load_assets(const std::string& game_dir) {
    std::string dir = game_dir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    Assets a;
    auto need = [&](const char* what, auto&& fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            throw dd2::Error(std::string("Cannot load ") + what + " from '" + game_dir + "': " + e.what());
        }
    };
    need("DAVE.EXE", [&] { a.exe = dd2::load_exe_from_dir(dir); });
    need("EGATILES.DD2", [&] { a.tiles = dd2::load_tileset(dir + "EGATILES.DD2"); });
    need("the sprite files (S_*.DD2)", [&] { a.sprites = dd2::load_sprites(dir); });
    for (int i = 0; i < 8; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "LEVEL%02d.DD2", i + 1);
        need(name, [&] { a.levels[size_t(i)] = dd2::load_level(dir + name); });
    }
    need("TITLE1.DD2", [&] { a.title1 = dd2::load_picture(dir + "TITLE1.DD2"); });
    need("TITLE2.DD2", [&] { a.title2 = dd2::load_picture(dir + "TITLE2.DD2"); });
    need("PROGPIC.DD2", [&] { a.progpic = dd2::load_picture(dir + "PROGPIC.DD2"); });
    return a;
}

bool ControlPanel::load(const std::string& path) {
    std::ifstream f(dd2::utf8_path(path), std::ios::binary);
    uint8_t b[14];
    if (!f || !f.read(reinterpret_cast<char*>(b), 14)) return false;
    for (int i = 0; i < 8; ++i) dir[size_t(i)] = b[i] & 0x7F;
    button1 = b[8] & 0x7F;
    button2 = b[9] & 0x7F;
    high_score = uint32_t(b[10]) | (uint32_t(b[11]) << 8) | (uint32_t(b[12]) << 16) | (uint32_t(b[13]) << 24);
    return true;
}

bool ControlPanel::save(const std::string& path) const {
    if (path.empty()) return false;
    std::ofstream f(dd2::utf8_path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    uint8_t b[14];
    for (int i = 0; i < 8; ++i) b[i] = dir[size_t(i)];
    b[8] = button1;
    b[9] = button2;
    for (int i = 0; i < 4; ++i) b[10 + i] = uint8_t(high_score >> (8 * i));
    f.write(reinterpret_cast<const char*>(b), 14);
    return bool(f);
}

// ---------------------------------------------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------------------------------------------

Game::Game(Machine& m, const Assets& a, GameOptions opt) : m_(m), s_(m.screen), a_(a), opt_(std::move(opt)) {
    if (!opt_.ctlpanel_path.empty()) panel.load(opt_.ctlpanel_path);
    world.reset_rng_on_load = false;
    int seed = opt_.rng_seed;
    if (seed < 0)                                       // 0x9183(1): DOS time, hundredths of a second
        seed = int(std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count() / 10 % 100);
    world.seed_rng(seed & 0xFF);
}

void Game::save_panel() { panel.save(opt_.ctlpanel_path); }

void Game::run() {
    s_.center_col = 0x13;                               // 0x18C9
    s_.center_row = 0x0B;
    if (opt_.skip_title) {
        level = opt_.start_level - 1;
        continuing_ = true;
        play_game();
    }
    for (;;) {                                          // 0x1931..0x193A
        in_game = false;
        title();
    }
}

// ----- palette fades (0x8208 / 0x8261): four steps of the palette table at DS:0x29EC, 6 retraces apart -----
void Game::set_palette_step(int step) {
    for (int i = 0; i < 16; ++i) s_.palette[size_t(i)] = a_.exe.ds_byte(uint16_t(0x29EC + step * 17 + i));
}
void Game::fade_in() {
    for (int i = 0; i < 4; ++i) { set_palette_step(i); m_.delay(6); }
}
void Game::fade_out() {
    for (int i = 3; i >= 0; --i) { set_palette_step(i); m_.delay(6); }
}

// ----- growing message box (0x84A3 and helpers): smaller boxes first, one retrace apart -----
void Game::grow_window(int w, int h) {
    if (w > 2) {
        if (h > 2) grow_window(w - 2, h - 2);
        else grow_w(w - 2, h);
    } else if (h > 2) {
        grow_h(w, h - 2);
    }
    m_.delay(1);
    s_.center_window(w, h);
}
void Game::grow_w(int w, int h) {
    if (w > 2) grow_w(w - 2, h);
    m_.delay(1);
    s_.center_window(w, h);
}
void Game::grow_h(int w, int h) {
    if (h > 2) grow_h(w, h - 2);
    m_.delay(1);
    s_.center_window(w, h);
}

void Game::draw_sprite_px(int n, int x, int y) {
    if (n >= 0 && size_t(n) < a_.sprites.size()) s_.sprite(a_.sprites[size_t(n)], x, y);
}

// ----- input -----
Game::Control Game::read_control() {
    Control c;
    const auto& k = m_.keydown;
    // 0x74AD: later keys override earlier ones.
    int dx = 0, dy = 0;
    const auto held = [&](uint8_t sc) { return k[sc & 0x7F] != 0; };
    if (held(panel.dir[0])) dy = -1;
    if (held(panel.dir[2])) dx = 1;
    if (held(panel.dir[4])) dy = 1;
    if (held(panel.dir[6])) dx = -1;
    if (held(panel.dir[1])) { dy = -1; dx = 1; }
    if (held(panel.dir[7])) { dy = -1; dx = -1; }
    if (held(panel.dir[3])) { dy = 1; dx = 1; }
    if (held(panel.dir[5])) { dy = 1; dx = -1; }
    static const int kDir[9] = {7, 0, 1, 6, 8, 2, 5, 4, 3};   // jump table at 0x75DE, index dy*3+dx+4
    c.dir = kDir[dy * 3 + dx + 4];
    c.b1 = held(panel.button1);
    c.b2 = held(panel.button2);
    if (opt_.extra_keys) {                              // port extras, only for keys not bound to anything else
        auto free = [&](int sc) {
            if (sc == panel.button1 || sc == panel.button2) return false;
            for (uint8_t d : panel.dir) if (d == sc) return false;
            return true;
        };
        if ((free(sc::kSpace) && k[sc::kSpace]) || (free(0x2C) && k[0x2C])) c.b1 = true;    // Space, Z
        if (free(0x2D) && k[0x2D]) c.b2 = true;                                             // X
    }
    if (m_.pad_connected) {                             // gamepad (replaces the PC joystick, see F4)
        if (m_.pad_dir != 8 && c.dir == 8) c.dir = m_.pad_dir;
        const bool jump = pad_b1_jumps_ ? m_.pad_b1 : m_.pad_b2;
        const bool fire = pad_b1_jumps_ ? m_.pad_b2 : m_.pad_b1;
        c.b1 = c.b1 || jump;
        c.b2 = c.b2 || fire;
    }
    return c;
}

int Game::get_key() {                                   // 0x8AAE
    const int x = s_.cur_x;
    m_.clear_keys();
    while (!m_.poll_key()) {
        s_.glyph('_');
        m_.delay(5);
        s_.cur_x = x;
        s_.glyph('_');
        s_.cur_x = x;
        if (m_.poll_key()) break;
        m_.delay(5);
    }
    s_.cur_x = x;
    const int key = m_.last_ascii;                      // 0x8E99(0): consume the press
    m_.last_scan &= 0x7F;
    return key;
}

void Game::wait_key() {                                 // 0x78F9
    m_.clear_keys();
    for (;;) {
        if (m_.last_scan > 0x7F) { m_.last_scan &= 0x7F; return; }
        const Control c = read_control();
        if (c.b1 || c.b2) return;
        m_.wait_vbl();
    }
}

static int upper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

// ---------------------------------------------------------------------------------------------------------------
// Title screen (0x154E): TITLE1 and TITLE2 side by side in an 80-byte-wide page; the view slides to TITLE2 and back.
// ---------------------------------------------------------------------------------------------------------------
void Game::title() {
    continuing_ = false;                                // 0x1555
    fade_out();
    s_.clear();
    s_.org_x = s_.org_y = 0;
    s_.view_x = s_.view_y = 0;
    s_.picture(a_.title1, 0, 0);                        // 0x1502: TITLE1 at byte 0, TITLE2 at byte 0x28
    s_.picture(a_.title2, 320, 0);
    const auto backup = s_.canvas;                      // copy of the page used to restore it after a window
    fade_in();
    int x = 0;
    for (int si = 0;;) {
        if (si > 200 && si <= 280) x += 4;
        if (si > 400 && si <= 480) x -= 4;
        if (si > 480) si = 0;
        s_.view_x = x;                                  // 0x939E: start address x>>3, pel pan x&7 (waits a retrace)
        s_.org_x = x & ~7;                              // DS:0x3172 follows the start address
        m_.wait_vbl();
        ++si;
        if (check_keys()) s_.canvas = backup;
        const Control c = read_control();
        if (c.b1 || c.b2 || m_.keydown[sc::kSpace]) break;
    }
    m_.clear_keys();
    m_.play_sound(22);                                  // LEVELSTART
    s_.org_x = 0;
    play_game();
}

// ---------------------------------------------------------------------------------------------------------------
// One game (0x2364): levels until the lives run out, the game is won, or F5 resets it.
// ---------------------------------------------------------------------------------------------------------------
void Game::play_game() {
    if (!continuing_) level = 0;
    for (;;) {                                          // 0x2379: a new game (also after "continue")
        reset_ = false;
        world.new_game();
        world.set_lives(4);
        bool next_game = false;
        for (;;) {
            ++level;                                    // 0x239D
            for (;;) {                                  // 0x23A7: (re)start the current level
                fade_out();
                if (!m_.keydown[sc::kEsc]) level_intro();
                m_.clear_keys();
                world.load(a_.levels[size_t(std::clamp(level, 1, 8) - 1)], level);   // 0xD87
                if (m_.tick_hook) wait_intro();
                fade_out();
                fade_in_pending_ = true;
                in_game = true;
                play_loop();
                in_game = false;
                if (reset_) break;
                if (world.level_state() < 0) {          // died
                    death_animation();
                    world.set_lives(world.lives() - 1);
                    if (world.lives() == 0) {
                        grow_window(0x11, 3);
                        s_.cur_y += 8;
                        s_.print_centered(str(0x821));  // G A M E   O V E R
                        m_.play_sound(9);
                        m_.wait_sound();
                        m_.delay(120);
                        break;
                    }
                    if (level >= 9) break;
                    continue;
                }
                ++level;                                // 0x252A: level complete
                m_.play_sound(11);
                if (level == 9) ending();
                if (level >= 9) break;
            }
            break;
        }
        high_score_and_continue(next_game);
        if (!next_game) return;
    }
}

void Game::high_score_and_continue(bool& again) {
    again = false;
    if (world.score() > panel.high_score) {             // 0x2552
        grow_window(0x12, 0x0B);
        s_.cur_y += 3;
        s_.print_centered(str(0x833));                  // New High Score!
        s_.cur_y += 5;
        s_.print_centered(str(0x844));                  // Score
        s_.print_centered(std::to_string(world.score()));
        s_.print(str(0x84B));
        s_.print_centered(str(0x84E));                  // Old
        s_.print_centered(std::to_string(panel.high_score));
        s_.print(str(0x853));
        s_.cur_y += 5;
        s_.print_centered(str(0x855));                  // Congratulations!
        s_.print_centered(str(0x867));
        wait_key();
        panel.high_score = world.score();
        save_panel();
    }
    if (!reset_ && level > 2 && level < 9) {            // 0x2685: continue from the previous level
        s_.window(7, 0x14, 0x20, 0x17);
        s_.cur_y += 3;
        s_.print_centered(str(0x868) + std::to_string(level - 1) + str(0x87D));
        m_.clear_keys();
        if (upper(get_key()) == 'Y') {
            level -= 2;
            continuing_ = true;
            again = true;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Level intro (0x1201): PROGPIC, "LEVEL n", lives, score, a marker on the map, "LOADING"; shown ~2.9 s.
// ---------------------------------------------------------------------------------------------------------------
void Game::level_intro() {
    s_.clear();
    s_.org_x = s_.org_y = 0;
    s_.view_x = s_.view_y = 0;
    s_.picture(a_.progpic, 0, 0);
    s_.margin_l = 0;
    s_.margin_r = 0x140;
    s_.cur_y = 5;
    s_.window(0, 0, 10, 3);
    s_.cur_y += 3;
    if (level >= 1 && level <= 8) s_.print_centered(str(uint16_t(0x554 + (level - 1) * 9)));   // LEVEL n
    s_.cur_y = 0xA5;
    const int lives = std::min(world.lives(), 8);
    for (int di = 1, x = 0x2C; di < lives; ++di, x += 16) draw_sprite_px(1, x, s_.cur_y);      // spare Daves
    s_.cur_x = 0xF4; s_.cur_y = 0xA6;
    s_.print(std::to_string(world.score()));
    s_.cur_x = 0xF4; s_.cur_y = 0xB9;
    s_.print(std::to_string(panel.high_score));
    fade_in();
    m_.clear_keys();
    const int lv = std::clamp(level, 1, 8);
    const int mx = int(a_.exe.ds_word(uint16_t(0x110 + lv * 2)));
    const int my = int(a_.exe.ds_word(uint16_t(0x122 + lv * 2)));
    draw_sprite_px(0x43, mx, my);                       // map marker (sprite 0x43, frame 0)
    intro_counter_ = intro_frames_ = 0;
    m_.tick_hook = [this] {                             // 0x11B7, every 140 Hz tick
        if (++intro_counter_ >= 20) { intro_counter_ = 0; ++intro_frames_; }
    };
    s_.window(0, 5, 10, 8);                             // the original always shows it (DS:0x4BEA is reset first)
    s_.cur_y += 3;
    s_.print_centered(str(0x59C));                      // LOADING
}

void Game::wait_intro() {                               // 0x11D6
    while (intro_frames_ < 20) {
        if (m_.last_scan & 0x80) { m_.clear_keys(); break; }
        if (m_.pad_connected && (m_.pad_b1 || m_.pad_b2)) break;
        m_.wait_vbl();
    }
    m_.tick_hook = nullptr;
}

// ---------------------------------------------------------------------------------------------------------------
// The play loop (0x205F)
// ---------------------------------------------------------------------------------------------------------------
void Game::render_play() {
    const Camera& cam = world.player().camera();
    const int col0 = cam.x >> 8, row0 = cam.y >> 8;
    s_.org_x = s_.org_y = 0;
    s_.view_x = (cam.x >> 4) & 15;                      // pel pan
    s_.view_y = (cam.y >> 4) & 15;                      // start address + fine rows
    const dd2::Level& lv = world.level();
    for (int r = 0; r < 14; ++r)
        for (int c = 0; c < 22; ++c) {
            const int tc = col0 + c, tr = row0 + r;
            const int t = (tc >= 0 && tr >= 0 && tc < lv.width && tr < lv.height) ? lv.tile_at(tc, tr) : 0;
            s_.tile16(a_.tiles, t, c * 16, r * 16);
        }
    const int ox = col0 * 16, oy = row0 * 16;
    auto spr = [&](int n, int px, int py, bool white) {
        if (n > 0 && size_t(n) < a_.sprites.size()) s_.sprite(a_.sprites[size_t(n)], px - ox, py - oy, white);
    };
    const auto& objs = world.objects();
    std::vector<ExtraDraw> extras;
    for (size_t i = objs.size(); i-- > 0;) {            // post-move order: reverse, the player last
        const Enemy& e = objs[i];
        if (!e.kind || !e.active) continue;
        spr(e.sprite(), e.draw_x(), e.draw_y(), e.flash > 0);   // hit flash: white silhouette (0x2F34 / 0x64BF)
        extras.clear();
        e.extra_draws(extras);
        for (const auto& x : extras)
            spr(x.sprite, (x.x + dd2::kSpriteTable[x.sprite].draw_dx) >> 4, (x.y + dd2::kSpriteTable[x.sprite].draw_dy) >> 4, false);
    }
    for (const auto& e : world.player().effects()) {
        const int n = e.facing > 0 ? e.state->sprite_e : e.state->sprite_w;
        spr(n, (e.x + dd2::kSpriteTable[n].draw_dx) >> 4, (e.y + dd2::kSpriteTable[n].draw_dy) >> 4, false);
    }
    spr(world.player().sprite(), world.player().draw_x(), world.player().draw_y(), false);
    // Ammo meter (0x2222 -> 0x304F): sprite AMMO0+ammo at 8,8 of the view, no draw offsets.
    const int ammo = std::clamp(world.player().ammo(), 0, 8);
    if (size_t(19 + ammo) < a_.sprites.size()) s_.sprite(a_.sprites[size_t(19 + ammo)], s_.view_x + 8, s_.view_y + 8);
}

void Game::play_loop() {
    for (;;) {
        const Control c = read_control();
        Input in;
        in.dir = c.dir;
        in.jump = c.b1;
        in.fire = c.b2;
        world.tick(in);
        for (int n : world.take_sounds()) m_.play_sound(n);
        render_play();
        m_.wait_vbl();
        if (fade_in_pending_) { fade_in_pending_ = false; fade_in(); }   // 0x2240
        if (show_tics_) {                               // 0x2250 (F12+S): single-step with a frame-time box
            s_.window(0x0F, 0, 0x18, 2);
            s_.print("1");
            s_.print(str(0x81B));                       // " tics"
            get_key();
            if ((m_.last_scan & 0x7F) == sc::kEsc) show_tics_ = false;
        }
        if (check_keys()) render_play();                // 0x22E8 (0x2B6D redraws after a window)
        const int st = world.level_state();
        if ((st != 0 && st != 100) || reset_) return;   // 0x2346
    }
}

// Death (0x2449): DEATHSOUND, then the matching 48x48 picture sequence over Dave (pictures 0..39 in DAVE.EXE).
void Game::death_animation() {
    m_.play_sound(10);
    int first = 0, count = 5;
    switch (world.level_state()) {                      // jump table at 0x2729
        case -1: first = 0; count = 4; break;           // knife (only four frames are shown)
        case -2: first = 5; break;                      // slime
        case -3: first = 10; break;                     // zombie
        case -4: first = 15; break;                     // spider
        case -5: first = 25; break;                     // ghost
        case -6: first = 20; break;                     // wolf
        case -7: first = 30; break;                     // skull / bosses / fire
        case -8: first = 35; break;                     // hazard tiles
        default: return;
    }
    const Camera& cam = world.player().camera();
    const int col = ((unsigned(world.player().x() - (cam.x & 0xFF00)) & 0xFFFF) >> 7) - 2;   // byte column
    const int y = int((unsigned(world.player().y() - (cam.y & 0xFF00)) & 0xFFFF) >> 4) - 8;
    for (int di = 1, si = first; di <= count; ++di, ++si) {
        if (size_t(si) < a_.exe.pics.size()) s_.picture(a_.exe.pics[size_t(si)], col * 8, y);
        m_.wait_vbl();                                  // (shown at the next retrace)
        if (m_.keydown[sc::kEsc]) break;
        m_.delay(20 - 1);
    }
}

// Ending (0x10AA): THE END over the last screen, then five story boxes.
void Game::ending() {
    if (a_.exe.pics.size() > 40) s_.picture(a_.exe.pics[40], 7 * 8, 8);
    m_.delay(0xB4);
    auto box = [&](int extra_y, std::initializer_list<uint16_t> lines) {
        grow_window(0x1C, 5);
        s_.cur_y += extra_y;
        for (uint16_t l : lines) s_.print_centered(str(l));
        m_.clear_keys();
        wait_key();
    };
    box(0, {0x409, 0x41B, 0x438, 0x44D});
    box(0, {0x45D, 0x474, 0x48F, 0x4A5});
    box(9, {0x4BB, 0x4D7});
    box(4, {0x4F5, 0x511, 0x52A});
    box(0x0E, {0x53F});
}

// ---------------------------------------------------------------------------------------------------------------
// Function keys (0x041C). Returns true when a key was handled (the caller then redraws).
// ---------------------------------------------------------------------------------------------------------------
void Game::help_window() {                              // 0x0239
    s_.center_window(0x1E, 0x16);
    s_.print_centered(str(0x13A));
    s_.print_centered(str(0x153));
    for (uint16_t a : {0x16D, 0x183, 0x1A1, 0x1B6, 0x1C8, 0x1D4, 0x1EA, 0x201, 0x21C, 0x233, 0x248}) s_.print(str(a));
    s_.print_centered(str(0x25E));
    get_key();
}

void Game::status_window() {                            // 0x055B
    grow_window(0x0E, 0x0E);
    s_.cur_y += 4;
    s_.print_centered(str(0x306));                      // Score
    s_.print_centered(std::to_string(world.score()));
    s_.print(str(0x30D));
    s_.print_centered(str(0x310));                      // High Score
    s_.print_centered(std::to_string(panel.high_score));
    s_.print(str(0x31C));
    s_.print_centered(str(0x31F));                      // Daves
    const int n = std::min(world.lives(), 8);
    for (int di = 1, x = 0xA4 - n * 8; di < n; ++di, x += 16) draw_sprite_px(1, x, s_.cur_y);
    wait_key();
}

void Game::key_name(int sc) {                           // 0x70FD
    sc &= 0x7F;
    struct N { int sc; uint16_t s; };
    static const N kNames[] = {{0x01, 0x2B57}, {0x0E, 0x2B5B}, {0x0F, 0x2B60}, {0x1D, 0x2B64}, {0x2A, 0x2B69},
                               {0x39, 0x2B70}, {0x3A, 0x2B76}, {0x57, 0x2B7F}, {0x58, 0x2B83}, {0x46, 0x2B87},
                               {0x1C, 0x2B8E}, {0x36, 0x2B94}, {0x37, 0x2B9B}, {0x38, 0x2BA1}, {0x47, 0x2BA5},
                               {0x49, 0x2BAA}, {0x4F, 0x2BAF}, {0x51, 0x2BB3}, {0x52, 0x2BB8}, {0x53, 0x2BBC},
                               {0x45, 0x2BC0}, {0x48, 0x2BC6}, {0x50, 0x2BC9}, {0x4B, 0x2BCE}, {0x4D, 0x2BD3}};
    if (sc >= 0x3B && sc <= 0x44) {                     // F1..F10
        s_.print(str(0x2B7D));
        s_.print(std::to_string(sc - 0x3A));
        return;
    }
    for (const N& n : kNames)
        if (n.sc == sc) { s_.print(str(n.s)); return; }
    const int c = a_.exe.ds_byte(uint16_t(0x2D7A + sc));
    if (c > ' ') s_.glyph(uint8_t(upper(c)));
}

void Game::keyboard_config() {                          // 0x7264
    grow_window(0x16, 0x0E);
    s_.print_centered(str(0x2BD9));
    s_.cur_y += 10;
    s_.print_centered(str(0x2BF0));
    for (uint16_t a : {0x2C07, 0x2C10, 0x2C18, 0x2C21, 0x2C29, 0x2C34, 0x2C3F}) s_.print(str(a));
    const int cur_col = (s_.cur_x + 7) >> 3, cur_row = s_.cur_y;
    const int text_top = (s_.center_row - 0x0E / 2 + 1) * 8;    // DS:0x9924 for this 0x16 x 0x0E box
    auto show = [&](int i, uint8_t sc) {
        s_.cur_x = s_.margin_l + 0x60;
        s_.cur_y = text_top + (i + 2) * 10;
        s_.print(str(0x2C56));
        key_name(sc);
    };
    for (int i = 0; i < 4; ++i) show(i, panel.dir[size_t(i * 2)]);
    show(4, panel.button1);
    show(5, panel.button2);
    for (;;) {
        s_.cur_x = cur_col * 8;
        s_.cur_y = cur_row;
        s_.tile8(cur_col, cur_row, 0x1B);
        const int key = get_key() & 0xFF;
        if (key < '1' || key > '6') {
            if (key >= '0' && key <= '9') continue;
            break;
        }
        const int si = key - '1';
        s_.glyph(uint8_t(key));
        s_.print(str(0x2C5C));                          // "\nPress the new key:"
        m_.clear_keys();
        int sc = -1;
        for (;;) {                                      // scan the key table for a held key (skips LSHIFT, 0x2A)
            ++sc;
            if (sc == 0x2A) ++sc;
            if (sc >= 0x7A) { sc = -1; m_.wait_vbl(); continue; }
            if (m_.keydown[size_t(sc)]) break;
        }
        // erase the prompt line: 0x95B2 fills 22 bytes x 10 rows with 0xFF (colour 15)
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 22 * 8; ++x) s_.put_o(s_.margin_l + x, s_.cur_y + y, 15);
        if (si < 4) panel.dir[size_t(si * 2)] = uint8_t(sc);
        if (si == 4) panel.button1 = uint8_t(sc);
        if (si == 5) panel.button2 = uint8_t(sc);
        s_.cur_x = s_.margin_l + 0x60;
        s_.cur_y = text_top + (si + 2) * 10;
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 9 * 8; ++x) s_.put_o((s_.cur_x & ~7) + x, s_.cur_y + y, 15);
        s_.print(str(0x2C70));
        key_name(sc);
        m_.clear_keys();
        save_panel();
    }
}

void Game::joystick_config() {                          // 0x6EBA (a gamepad needs no calibration)
    grow_window(0x18, 0x10);
    s_.print_centered(str(0x2A7A));
    s_.print_centered(str(0x2A92));
    s_.print(str(0x2B2F));                              // "Should button 1 (A)ttack, or (J)ump ?"
    const int k = upper(get_key());
    pad_b1_jumps_ = k != 'A';
}

bool Game::check_keys() {
    if (m_.last_scan == 0) return false;
    const int key = m_.last_scan & 0x7F;
    auto yes_no = [&](int w, uint16_t prompt) {
        m_.clear_keys();
        grow_window(w, 1);
        s_.print(str(prompt));
        return upper(get_key());
    };
    switch (key) {
        case sc::kF1: m_.clear_keys(); help_window(); break;
        case sc::kF2: {
            const int k = yes_no(0x0D, 0x2DA);
            if (k == 'N') m_.sound_enabled = false;
            else if (k == 'Y') m_.sound_enabled = true;
            break;
        }
        case sc::kF3: m_.clear_keys(); keyboard_config(); break;
        case sc::kF4: m_.clear_keys(); joystick_config(); break;
        case sc::kF5:
            if (yes_no(0x12, 0x2E7) == 'Y') reset_ = true;
            break;
        case sc::kEsc:
            if (yes_no(0x0C, 0x2F9) == 'Y') {
                save_panel();
                if (opt_.on_quit) opt_.on_quit("");
                throw QuitProgram{};
            }
            break;
        case sc::kTab:
            if (!in_game) return false;
            m_.clear_keys();
            status_window();
            break;
        case sc::kF12: {                                // 0x0341: debug keys, held together with F12
            const auto& k = m_.keydown;
            if (k[0x22]) {                              // G: god mode
                grow_window(0x0C, 1);
                s_.print_centered(str(world.god ? 0x2A7 : 0x2B4));
                wait_key();
                world.god = !world.god;
            } else if (k[0x19]) {                       // P: pause
                wait_key();
            } else if (k[0x1F]) {                       // S: single-step with a tics box
                show_tics_ = true;
            } else if (k[0x11] && in_game) {            // W: warp
                grow_window(0x18, 1);
                s_.print(str(0x2C0));
                const int c = get_key();
                if (c >= '1' && c <= '8') {
                    level = c - '1';
                    world.set_level_state(1);
                }
            } else if (k[0x21] && in_game) {            // F: 1600-point bonus
                world.add_score_popup(5);
            } else {
                return false;
            }
            break;
        }
        default: return false;
    }
    m_.clear_keys();
    return true;
}

}  // namespace ddhm
