// SPDX-License-Identifier: MIT
// Screenshot regression: plays fixed scripts through the whole program headless and compares a CRC of the visible
// screen at chosen frames with tests/screens.golden. Only CRCs are stored, never images (they would be game art).
// After an intended visual change, regenerate with:  test_screens <game-dir> <golden-file> --update
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "game/flow.hpp"
#include "game/machine.hpp"

using namespace ddhm;

namespace {
uint32_t crc32(const std::vector<uint8_t>& d) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : d) {
        c ^= b;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

struct Script {
    const char* name;
    int level;                                   // 0 = start at the title
    std::vector<std::pair<int, int>> keys;       // frame, +sc down / -sc up
    std::vector<int> shots;                      // frames to hash
    int complete_at = -1, die_at = -1;
};

std::vector<Script> scripts() {
    std::vector<Script> s;
    s.push_back({"title", 0, {{20, sc::kF1}, {21, -sc::kF1}, {120, sc::kEsc}, {121, -sc::kEsc}, {200, sc::kSpace}, {201, -sc::kSpace}},
                 {50, 100, 160, 260, 330}});
    s.push_back({"level1", 0, {{60, sc::kSpace}, {61, -sc::kSpace}}, {120, 200, 300}});   // level intro
    s.push_back({"walk1", 1, {{300, sc::kRight}, {420, sc::kCtrl}, {425, -sc::kCtrl}, {480, sc::kAlt}, {481, -sc::kAlt},
                              {600, -sc::kRight}},
                 {300, 360, 430, 500, 640}});
    s.push_back({"frank", 4, {{300, sc::kLeft}, {380, -sc::kLeft}}, {300, 400, 500}});
    s.push_back({"master", 8, {{300, sc::kRight}, {340, -sc::kRight}}, {300, 400}});
    Script d{"death", 2, {}, {360, 380, 420, 460}};
    d.die_at = 350;
    s.push_back(d);
    Script e{"ending", 8, {{600, sc::kSpace}, {601, -sc::kSpace}}, {500, 640}};
    e.complete_at = 300;
    s.push_back(e);
    Script t{"status", 3, {{330, sc::kTab}, {331, -sc::kTab}}, {360}};
    s.push_back(t);
    return s;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: test_screens <game-dir> <golden-file> [--update]\n"); return 2; }
    const bool update = argc > 3 && std::string(argv[3]) == "--update";
    Assets a;
    try {
        a = load_assets(argv[1]);
    } catch (const std::exception& e) {
        std::printf("FAIL cannot load the game: %s\n", e.what());
        return 1;
    }
    std::map<std::string, uint32_t> golden;
    if (!update) {
        std::ifstream f(argv[2]);
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            std::string k;
            uint32_t v = 0;
            if (ls >> k >> std::hex >> v) golden[k] = v;
        }
        if (golden.empty()) { std::printf("FAIL no golden values in %s (run with --update)\n", argv[2]); return 1; }
    }
    std::map<std::string, uint32_t> got;
    for (const Script& sc : scripts()) {
        Machine m(a.exe);
        GameOptions opt;
        opt.rng_seed = 0;
        opt.skip_title = sc.level > 0;
        opt.start_level = sc.level > 0 ? sc.level : 1;
        Game* game = nullptr;
        m.start([&](Machine& mm) { Game g(mm, a, opt); game = &g; g.run(); });
        int last = 0;
        for (int f : sc.shots) last = std::max(last, f);
        for (int f = 0; f <= last; ++f) {
            for (const auto& [kf, k] : sc.keys)
                if (kf == f) m.key_event(k > 0 ? k : -k, k > 0);
            if (game && f == sc.complete_at) game->world.set_level_state(1);
            if (game && f == sc.die_at) game->world.set_level_state(-3);
            if (!m.run_frame()) break;
            for (int s : sc.shots)
                if (s == f) {
                    std::vector<uint8_t> v;
                    m.screen.visible(v);
                    char key[64];
                    std::snprintf(key, sizeof key, "%s@%d", sc.name, f);
                    got[key] = crc32(v);
                }
        }
    }
    if (update) {
        std::ofstream f(argv[2]);
        f << "# screen CRCs (palette indices of the visible 320x200 area) - regenerate with test_screens --update\n";
        for (const auto& [k, v] : got) { char line[96]; std::snprintf(line, sizeof line, "%s %08x\n", k.c_str(), v); f << line; }
        std::printf("wrote %zu values to %s\n", got.size(), argv[2]);
        return 0;
    }
    int fail = 0;
    for (const auto& [k, v] : got) {
        auto it = golden.find(k);
        if (it == golden.end()) { std::printf("FAIL %s: no golden value\n", k.c_str()); ++fail; }
        else if (it->second != v) { std::printf("FAIL %s: crc %08x, expected %08x\n", k.c_str(), v, it->second); ++fail; }
    }
    if (fail) std::printf("%d screen(s) differ\n", fail);
    else std::printf("all %zu screens match\n", got.size());
    return fail ? 1 : 0;
}
