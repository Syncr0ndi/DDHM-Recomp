// SPDX-License-Identifier: MIT
// ddhm_run - runs the whole game headless with scripted key presses and records what it shows and plays.
//
//   ddhm_run <game-dir> [options]
//     --frames N            retraces to run (70 per second)
//     --level N             skip the title and start a game on level N
//     --seed N              fixed RNG seed (default 0, deterministic)
//     --key F:+SC / F:-SC   press / release PC scan code SC (hex) at frame F; F:=SC = press for one frame
//     --png F:path          save the visible screen at frame F
//     --wav path            write the PC-speaker output
//     --hash                print a CRC of the visible screen every 70 frames
//     --god                 start with god mode on
//     --complete F          finish the current level at frame F (as if Dave reached the exit)
// Exit code 0 when the run completed; the last line printed is a summary (level, lives, score, state).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "dd2/dd2.hpp"
#include "game/flow.hpp"
#include "game/machine.hpp"
#include "game/speaker.hpp"

namespace {
uint32_t crc32(const std::vector<uint8_t>& d) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : d) {
        c ^= b;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}
void write_wav(const std::string& path, const std::vector<int16_t>& s, int rate) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(uint32_t(36 + s.size() * 2)); f.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(1); u32(uint32_t(rate)); u32(uint32_t(rate * 2)); u16(2); u16(16);
    f.write("data", 4); u32(uint32_t(s.size() * 2));
    f.write(reinterpret_cast<const char*>(s.data()), std::streamsize(s.size() * 2));
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: ddhm_run <game-dir> [--frames N] [--level N] [--key F:+SC] [--png F:path] [--wav path] [--hash]\n"); return 2; }
    const std::string dir = argv[1];
    long frames = 700;
    int level = 0, seed = 0;
    bool hash = false, god = false;
    long complete_at = -1;                              // --complete F: mark the current level finished at frame F
    std::string wav;
    std::multimap<long, std::pair<int, int>> keys;     // frame -> (scancode, +1 down / -1 up / 0 tap)
    std::multimap<long, std::string> pngs;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(2); } return argv[++i]; };
        if (a == "--frames") frames = std::atol(next().c_str());
        else if (a == "--level") level = std::atoi(next().c_str());
        else if (a == "--seed") seed = std::atoi(next().c_str());
        else if (a == "--hash") hash = true;
        else if (a == "--god") god = true;
        else if (a == "--complete") complete_at = std::atol(next().c_str());
        else if (a == "--wav") wav = next();
        else if (a == "--key") {
            const std::string v = next();
            const size_t c = v.find(':');
            const long f = std::atol(v.substr(0, c).c_str());
            const char op = v[c + 1];
            const int sc = int(std::strtol(v.substr(c + 2).c_str(), nullptr, 16));
            keys.insert({f, {sc, op == '+' ? 1 : (op == '-' ? -1 : 0)}});
        } else if (a == "--png") {
            const std::string v = next();
            const size_t c = v.find(':');
            pngs.insert({std::atol(v.substr(0, c).c_str()), v.substr(c + 1)});
        } else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    ddhm::Assets assets;
    try {
        assets = ddhm::load_assets(dir);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    ddhm::Machine m(assets.exe);
    ddhm::GameOptions opt;
    opt.rng_seed = seed;
    opt.skip_title = level > 0;
    opt.start_level = level > 0 ? level : 1;
    ddhm::Game* game_ptr = nullptr;
    m.start([&](ddhm::Machine& mm) {
        ddhm::Game game(mm, assets, opt);
        game.world.god = god;
        game_ptr = &game;
        game.run();
    });
    ddhm::Speaker spk;
    std::vector<int16_t> pcm;
    std::vector<std::pair<int, long>> taps;            // keys to release one frame later
    long f = 0;
    for (; f < frames; ++f) {
        for (auto it = taps.begin(); it != taps.end();) {
            if (it->second == f) { m.key_event(it->first, false); it = taps.erase(it); } else ++it;
        }
        auto range = keys.equal_range(f);
        for (auto it = range.first; it != range.second; ++it) {
            const auto [sc, op] = it->second;
            m.key_event(sc, op >= 0);
            if (op == 0) taps.push_back({sc, f + 1});
        }
        if (f == complete_at && game_ptr) game_ptr->world.set_level_state(1);
        if (!m.run_frame()) break;
        for (uint16_t d : m.speaker) spk.tick(d, pcm);
        m.speaker.clear();
        auto pr = pngs.equal_range(f);
        for (auto it = pr.first; it != pr.second; ++it) {
            const auto img = m.screen.rgba();
            dd2::write_png_rgba(it->second, ddhm::Screen::kW, ddhm::Screen::kH, img.data());
        }
        if (game_ptr && !m.finished()) {                 // trace level / state / lives changes
            static int pl = -99, ps = -99, pv = -99;
            const int l = game_ptr->level, st = game_ptr->world.level_state(), lv = game_ptr->world.lives();
            if (l != pl || st != ps || lv != pv) std::printf("frame %ld: level %d state %d lives %d score %u\n", f, l, st, lv, game_ptr->world.score());
            pl = l; ps = st; pv = lv;
        }
        if (hash && f % 70 == 69) {
            std::vector<uint8_t> v;
            m.screen.visible(v);
            std::printf("frame %ld crc %08x\n", f + 1, crc32(v));
        }
    }
    if (!wav.empty()) write_wav(wav, pcm, spk.rate());
    if (game_ptr && !m.finished())
        std::printf("frames %ld level %d lives %d score %u state %d\n", f, game_ptr->level, game_ptr->world.lives(),
                    game_ptr->world.score(), game_ptr->world.level_state());
    else
        std::printf("frames %ld program ended\n", f);
    return 0;
}
