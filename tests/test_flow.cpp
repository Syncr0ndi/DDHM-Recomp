// SPDX-License-Identifier: MIT
// Tests of everything around the play loop: the data read from DAVE.EXE, the screen / text primitives, the PC model
// (timer, keys, speaker) and the game program itself, run headless frame by frame. Expected values come from the
// disassembly (addresses next to each check); timing checks count 70 Hz retraces.
// usage: test_flow <game-dir>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "game/flow.hpp"
#include "game/machine.hpp"
#include "game/speaker.hpp"

using namespace ddhm;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)
#define CHECK_RANGE(v, lo, hi) do { const long v_ = long(v); if (v_ < long(lo) || v_ > long(hi)) { std::printf("FAIL %s:%d  %s = %ld, expected %ld..%ld\n", __FILE__, __LINE__, #v, v_, long(lo), long(hi)); ++g_fail; } } while (0)

// Runs the program headless; frame() = one retrace.
struct Run {
    Machine m;
    Game* game = nullptr;
    std::vector<uint16_t> spk;                    // every speaker tick so far
    Run(const Assets& a, GameOptions opt) : m(a.exe) {
        m.start([this, &a, opt](Machine& mm) {
            Game g(mm, a, opt);
            game = &g;
            g.run();
        });
    }
    bool frame() {
        const bool ok = m.run_frame();
        spk.insert(spk.end(), m.speaker.begin(), m.speaker.end());
        m.speaker.clear();
        return ok;
    }
    void frames(int n) { while (n-- > 0) frame(); }
    void tap(int sc) { m.key_event(sc, true); frame(); m.key_event(sc, false); }
    template <class F> long until(F cond, long limit) {   // frames until cond() holds (or -1)
        for (long i = 1; i <= limit; ++i) { frame(); if (cond()) return i; }
        return -1;
    }
    bool played(const dd2::Sound& s, size_t from = 0) const {   // the sound's first tones appear in the speaker log
        if (s.divisors.empty()) return false;
        size_t k = 0;
        while (k < s.divisors.size() && s.divisors[k] == 0) ++k;
        if (k >= s.divisors.size()) return false;
        for (size_t i = from; i + 1 < spk.size(); ++i)
            if (spk[i] == s.divisors[k] && (k + 1 >= s.divisors.size() || spk[i + 1] == s.divisors[k + 1])) return true;
        return false;
    }
};

static void test_exe_data(const Assets& a) {
    const dd2::ExeData& e = a.exe;
    CHECK(e.tile8.count == 32);                            // graphics header +0x10
    CHECK(e.pics.size() == 41);                            // graphics header +0x28
    if (e.pics.size() == 41) {
        CHECK(e.pics[0].width == 48 && e.pics[0].height == 48);
        CHECK(e.pics[40].width == 208 && e.pics[40].height == 56);
        CHECK(e.pics[40].name == "THEEND");
    }
    CHECK(e.font.height == 10);                            // fontstruct at 0x106D0
    CHECK(e.font.width['A'] == 8 && e.font.width[' '] == 5 && e.font.width['i'] == 4);
    CHECK(e.font.text_width("AB") == 16);
    CHECK(e.font.text_width(std::string("\x7F") + "BA") == 8);   // colour escape and its argument take no space
    CHECK(e.sounds.size() == 25);                          // "SND" header: 24 sounds
    if (e.sounds.size() == 25) {
        CHECK(e.sounds[3].name == "JUMP" && e.sounds[10].name == "DEATHSOUND" && e.sounds[24].name == "HITHEAD");
        CHECK(e.sounds[9].priority == 0xFF && e.sounds[3].priority == 0x80);
        for (size_t i = 1; i < e.sounds.size(); ++i) CHECK(!e.sounds[i].divisors.empty());
    }
    CHECK(e.ds_string(0x821) == "G A M E   O V E R");
    CHECK(e.ds_string(0x2CD7) == "CTLPANEL.DD2");
    // Window tiles: inside (0x1B) is white, tile 0 is black.
    CHECK(e.tile8.tile(0x1B)[0] == 15 && e.tile8.tile(0)[0] == 0);
}

static void test_screen(const Assets& a) {
    Screen s(&a.exe);
    s.window(2, 3, 12, 6);                                 // 0x82BE
    CHECK(s.margin_l == 24 && s.margin_r == 96 && s.cur_x == 24 && s.cur_y == 32);
    CHECK(s.at(3 * 8 + 4, 4 * 8 + 4) == 15);               // inside is white
    s.center_window(0x11, 3);                              // 0x8424: x0 = 19 - 8, y0 = 11 - 1
    CHECK(s.margin_l == (11 + 1) * 8 && s.margin_r == (11 + 0x11 + 1) * 8 && s.cur_y == 11 * 8);
    const int w = a.exe.font.text_width("GAME");
    s.print_centered("GAME");                              // 0x8B46: (r - l - w) / 2
    CHECK(s.cur_x == s.margin_l + (s.margin_r - s.margin_l - w) / 2 + w);
    // XOR text: drawing a glyph twice restores the background (the blinking cursor of 0x8AAE relies on it)
    const auto before = s.canvas;
    s.cur_x = 100; s.cur_y = 100; s.glyph('_');
    CHECK(s.canvas != before);
    s.cur_x = 100; s.glyph('_');
    CHECK(s.canvas == before);
    s.print(std::string("\x7F") + "E");                    // colour escape: 'E' - 'A' = 4
    CHECK(s.color == 4);
    // RGBI palette: register 6 is brown, 24 is dark grey (bit 4 = intensity)
    CHECK(Screen::ega_rgb(6).r == 170 && Screen::ega_rgb(6).g == 85);
    CHECK(Screen::ega_rgb(24).r == 85 && Screen::ega_rgb(24).b == 85);
}

static void test_machine(const Assets& a) {
    Machine m(a.exe);
    m.play_sound(3);                                       // JUMP, priority 0x80
    m.timer_tick();
    CHECK(m.speaker.back() == a.exe.sounds[3].divisors[0]);
    m.play_sound(4);                                       // LAND, priority 0xC8 >= 0x80: replaces
    m.timer_tick();
    CHECK(m.speaker.back() == a.exe.sounds[4].divisors[0]);
    m.play_sound(3);                                       // lower priority: ignored (0x8FA8)
    m.timer_tick();
    CHECK(m.speaker.back() == a.exe.sounds[4].divisors[1]);
    for (int i = 0; i < 20000 && m.sound_playing(); ++i) m.timer_tick();
    CHECK(!m.sound_playing());
    m.play_sound(3);                                       // priority resets when a sound ends
    CHECK(m.sound_playing());
    m.stop_sound();
    m.sound_enabled = false;                               // F2 -> N: the sound runs silently
    m.play_sound(9);
    m.speaker.clear();
    m.timer_tick();
    CHECK(m.speaker.back() == 0 && m.sound_playing());
    // Keyboard interrupt: press sets the table, last scan code with bit 7, and ASCII from the EXE's table.
    m.key_event(0x15, true);                               // Y
    CHECK(m.keydown[0x15] == 1 && m.last_scan == 0x95 && m.poll_key() == 'y');
    m.key_event(0x15, false);
    CHECK(m.keydown[0x15] == 0 && m.last_scan == 0x95);
    m.clear_keys();
    CHECK(m.last_scan == 0 && m.poll_key() == 0);
    // Speaker: a 1000 Hz divisor gives ~1000 cycles per second of square wave.
    Speaker spk(44100);
    std::vector<int16_t> pcm;
    for (int i = 0; i < 140; ++i) spk.tick(1193, pcm);
    int rises = 0;
    for (size_t i = 1; i < pcm.size(); ++i) rises += (pcm[i - 1] < 0 && pcm[i] > 0);
    CHECK_RANGE(pcm.size(), 44000, 44200);                 // 140 ticks = 0.9996 s
    CHECK_RANGE(rises, 990, 1005);
}

static void test_program(const Assets& a) {
    GameOptions opt;
    opt.rng_seed = 0;
    {   // Title -> SPACE -> level 1 intro -> play.
        Run r(a, opt);
        r.frames(60);                                       // fade out (24) + fade in (24): palette back to normal
        CHECK(r.m.screen.palette[15] == 31);
        CHECK(r.game && !r.game->in_game);
        r.m.key_event(sc::kSpace, true);
        const size_t from = r.spk.size();
        const long t = r.until([&] { return r.game->in_game; }, 600);
        r.m.key_event(sc::kSpace, false);
        // fade out 24 + fade in 24 + intro wait 20 x 20 ticks = 200 retraces + fade out 24 (0x2364 / 0x1201 / 0x11D6)
        CHECK_RANGE(t, 270, 276);
        CHECK(r.played(a.exe.sounds[22], from));            // LEVELSTART (0x163F)
        CHECK(r.game->level == 1 && r.game->world.lives() == 4);
        r.frames(30);                                      // first frame fades in (0x2240)
        CHECK(r.m.screen.palette[15] == 31);
        // Ammo meter: AMMO8 at 8,8 of the view (0x2222)
        std::vector<uint8_t> vis;
        r.m.screen.visible(vis);
        const dd2::Sprite& am = a.sprites[19 + 8];
        int same = 0, opaque = 0;
        for (int y = 0; y < am.height; ++y)
            for (int x = 0; x < am.width; ++x) {
                const uint8_t c = am.pixels[size_t(y * am.width + x)];
                if (c == dd2::Sprite::kTransparent) continue;
                ++opaque;
                same += vis[size_t((8 + y) * 320 + 8 + x)] == c;
            }
        CHECK(opaque > 0 && same == opaque);
        // Jump: JUMP sound (0x3BE6)
        const size_t j0 = r.spk.size();
        r.tap(sc::kCtrl);
        r.frames(5);
        CHECK(r.played(a.exe.sounds[3], j0));
        // Death: DEATHSOUND, 5 pictures x 20 retraces, then a life is gone and the level restarts (0x2449)
        r.frames(60);
        const size_t d0 = r.spk.size();
        r.game->world.set_level_state(-3);
        const long t2 = r.until([&] { return r.game->world.lives() == 3; }, 400);
        CHECK_RANGE(t2, 100, 102);
        CHECK(r.played(a.exe.sounds[10], d0));
        const long t3 = r.until([&] { return r.game->in_game; }, 600);
        CHECK(t3 > 0 && r.game->level == 1);
        // Level complete: LEVELDONE, next level (0x252A)
        r.frames(40);
        const size_t c0 = r.spk.size();
        r.game->world.set_level_state(1);
        r.until([&] { return r.game->level == 2; }, 10);
        CHECK(r.game->level == 2);
        r.frames(3);
        CHECK(r.played(a.exe.sounds[11], c0));
        // Game over on the last life: GAMEOVER, then "Continue from level 1 ?" is not offered below level 3.
        r.until([&] { return r.game->in_game; }, 600);
        r.frames(40);
        r.game->world.set_lives(1);
        const size_t g0 = r.spk.size();
        r.game->world.set_level_state(-8);
        r.frames(100 + 30);
        CHECK(r.played(a.exe.sounds[9], g0));
        r.frames(600);                                     // sound + 120 retraces, then straight back to the title
        CHECK(!r.game->in_game);
    }
    {   // Continue: game over on level 4 offers level 3 (0x2685); Y restarts there with 4 lives and no score.
        GameOptions o = opt;
        o.skip_title = true;
        o.start_level = 4;
        Run r(a, o);
        r.until([&] { return r.game->in_game; }, 600);
        r.frames(40);
        r.game->world.set_lives(1);
        r.game->world.set_level_state(-1);
        r.frames(1200);
        r.tap(sc::kY);
        const long t = r.until([&] { return r.game->in_game; }, 600);
        CHECK(t > 0 && r.game->level == 3 && r.game->world.lives() == 4 && r.game->world.score() == 0);
    }
    {   // Debug keys: hold G and press F12 -> god mode; firing then keeps all eight shells (0x41C0).
        GameOptions o = opt;
        o.skip_title = true;
        Run r(a, o);
        r.until([&] { return r.game->in_game; }, 600);
        r.frames(40);
        r.m.key_event(0x22, true);
        r.tap(sc::kF12);
        r.m.key_event(0x22, false);
        r.frames(20);
        r.tap(sc::kSpace);                                 // dismiss "God mode on"
        r.frames(5);
        CHECK(r.game->world.god);
        r.tap(sc::kAlt);
        r.frames(40);
        CHECK(r.game->world.player().ammo() == 8);
        // TAB opens the status window and waits; the game does not advance meanwhile.
        r.tap(sc::kTab);
        r.frames(30);
        const int x = r.game->world.player().x();
        r.m.key_event(sc::kRight, true);
        r.frames(10);
        CHECK(r.game->world.player().x() == x);           // the arrow press closed the window (and cleared the keys)...
        r.m.key_event(sc::kRight, true);                  // ...the keyboard's auto-repeat sets the key again...
        r.frames(30);
        CHECK(r.game->world.player().x() != x);           // ...and then Dave walks
        r.m.key_event(sc::kRight, false);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: test_flow <game-dir>\n"); return 2; }
    Assets a;
    try {
        a = load_assets(argv[1]);
    } catch (const std::exception& e) {
        std::printf("FAIL cannot load the game: %s\n", e.what());
        return 1;
    }
    test_exe_data(a);
    test_screen(a);
    test_machine(a);
    test_program(a);
    std::printf(g_fail ? "%d check(s) FAILED\n" : "all flow checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
