// SPDX-License-Identifier: MIT
// Regression test: asserts every format invariant discovered during reverse engineering.
// usage: test_assets <game-dir>
#include "dd2/assets.hpp"
#include "dd2/exe.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "dd2/dd2.hpp"

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main(int argc, char** argv) {
    // The game tables (sprites, states, collision, RNG) come from the user's DAVE.EXE, never from this project.
    if (argc < 2) { std::printf("usage: %s <game-dir or Assets-dir> ...\n", argv[0]); return 2; }
    try { dd2::load_exe_from_dir(argv[1]); } catch (const std::exception& e) { std::printf("cannot load DAVE.EXE: %s\n", e.what()); return 1; }
    if (argc < 2) { std::fprintf(stderr, "usage: %s <game-dir>\n", argv[0]); return 2; }
    const std::string dir = std::string(argv[1]) + "/";
    try {
        const auto ts = dd2::load_tileset(dir + "EGATILES.DD2");
        CHECK(ts.count == 858);

        struct Exp { int w, h; } exp[8] = {{64,57},{80,40},{60,80},{24,17},{56,115},{85,84},{80,84},{24,47}};
        for (int n = 1; n <= 8; ++n) {
            char name[32]; std::snprintf(name, sizeof name, "LEVEL%02d.DD2", n);
            const auto lv = dd2::load_level(dir + name);
            CHECK(lv.width == exp[n-1].w && lv.height == exp[n-1].h);
            CHECK(lv.layers == 2);
            for (uint16_t t : lv.tiles) CHECK(t < ts.count);          // every tile index is valid
        }
        for (const char* p : {"TITLE1", "TITLE2", "STARPIC", "PROGPIC"}) {
            const auto pic = dd2::load_picture(dir + p + std::string(".DD2"));
            CHECK(pic.width == 320 && pic.height == 200);
        }
        {   // INTRO.DD2: byte RLE (0x7EFC) must expand to exactly four 32x64 planes
            const auto raw = dd2::rleb_expand(dd2::read_file(dir + "INTRO.DD2"));
            CHECK(raw.size() == 4 * 32 * 64);
            const auto intro = dd2::load_intro(dir + "INTRO.DD2");
            CHECK(intro.width == 256 && intro.height == 64);
        }
        // Sprite sheets: Huffman stream must expand to exactly the declared length.
        const struct { const char* n; size_t len; } spr[] = {
            {"S_DAVE",25282},{"S_FRANK",18042},{"S_MASTER",18562},{"S_CHUNK1",16922},{"S_CHUNK2",14842}};
        for (auto& s : spr) CHECK(dd2::huff_expand(dd2::read_file(dir + s.n + std::string(".DD2"))).size() == s.len);
        // Sprites: table must exactly fill every plane (load_sprites throws otherwise).
        const auto sprites = dd2::load_sprites(dir);
        CHECK(int(sprites.size()) == dd2::kSpriteCount);
        CHECK(std::string(sprites[0].info->name) == "STARTDAVE");
        CHECK(std::string(sprites[1].info->name) == "DAVESTANDE");
        CHECK(sprites[1].width == 24 && sprites[1].height == 32);
        size_t opaque = 0;
        for (auto p : sprites[1].pixels) if (p != dd2::Sprite::kTransparent) ++opaque;
        CHECK(opaque > 100 && opaque < size_t(24 * 32));              // Dave is neither empty nor a solid block
        // Entities: counts below were tabulated independently from the raw layer-1 data.
        {
            int zombies = 0, total_spawns = 0, players = 0, franks = 0, masters = 0, delberts = 0;
            for (int n = 1; n <= 8; ++n) {
                char name[32]; std::snprintf(name, sizeof name, "LEVEL%02d.DD2", n);
                const auto lv = dd2::load_level(dir + name);
                const auto ents = dd2::extract_entities(lv);
                int p = 0;
                for (const auto& s : ents.spawns) {
                    ++total_spawns;
                    if (s.def->code == 0x01) ++zombies;
                    if (s.def->code == 0xFF) ++p;
                    if (s.def->code == 0x0B) ++franks;
                    if (s.def->code == 0x0C) ++masters;
                    if (s.def->code == 0x0D) ++delberts;
                    CHECK(s.sprite >= 0 && s.sprite < dd2::kSpriteCount);
                }
                CHECK(p == 1);                                   // exactly one player start per level
                CHECK(ents.player_index >= 0);
                if (n == 1) { int z = 0; for (auto& s : ents.spawns) z += s.def->code == 1; CHECK(z == 14); CHECK(ents.triggers.size() == 12); }
            }
            players = 8;
            CHECK(zombies == 129);
            CHECK(franks == 1 && masters == 1 && delberts == 1);
            CHECK(total_spawns == 129 + 84 + 28 + 10 + 14 + 26 + 4 + 17 + 1 + 1 + 1 + 132 + 13 + 11 + 10 + 10 + 4 + players);
        }
        // Tile behaviour tables (extracted from the EXE's tile info block).
        CHECK(dd2::tile_count_with_face(dd2::TileFace::Top) == 145);
        CHECK(dd2::tile_count_with_face(dd2::TileFace::Bottom) == 68);
        CHECK(dd2::tile_count_with_face(dd2::TileFace::Left) == 68);
        CHECK(dd2::tile_count_with_face(dd2::TileFace::Right) == 68);
        for (int t = 0; t < dd2::kTileCount; ++t) {                  // every fully solid tile is also a floor
            if (dd2::tile_face_solid(t, dd2::TileFace::Bottom)) CHECK(dd2::tile_face_solid(t, dd2::TileFace::Top));
        }
        CHECK(dd2::tile_property(265) == dd2::kPropDoor);            // white door
        CHECK(dd2::tile_property(122) == dd2::kPropHazard);          // bloody trap tile
        CHECK(dd2::tile_property(17) == dd2::kPropExitZone);
        CHECK(dd2::tile_property(0) == 0);
    } catch (const std::exception& e) { std::printf("EXCEPTION: %s\n", e.what()); return 1; }
    // Game -> Assets extraction: complete, self-contained, and changes are detected.
    try {
        namespace fs = std::filesystem;
        const fs::path tmp = fs::temp_directory_path() / "ddhm_test_assets";
        std::error_code ec;
        fs::remove_all(tmp, ec);
        CHECK(!dd2::check_assets(dd2::path_utf8(tmp)).ok);
        CHECK(!dd2::check_game_folder(dd2::path_utf8(tmp / "nothing")).ok);
        CHECK(dd2::check_game_folder(argv[1]).ok);
        dd2::extract_assets(argv[1], dd2::path_utf8(tmp));
        CHECK(dd2::check_assets(dd2::path_utf8(tmp)).ok);
        CHECK(fs::file_size(tmp / "DAVEDATA.BIN") == 215360);
        const auto from_assets = dd2::load_exe_from_dir(dd2::path_utf8(tmp));        // loads the unpacked image
        CHECK(from_assets.sounds.size() == 25 && from_assets.pics.size() == 41);
        CHECK(int(dd2::load_sprites(dd2::path_utf8(tmp) + "/").size()) == dd2::kSpriteCount);
        { std::ofstream f(tmp / "LEVEL03.DD2", std::ios::binary | std::ios::app); f << 'x'; }   // tamper
        CHECK(!dd2::check_assets(dd2::path_utf8(tmp)).ok);
        fs::remove(tmp / "MANIFEST.TXT", ec);
        CHECK(!dd2::check_assets(dd2::path_utf8(tmp)).ok);
        fs::remove_all(tmp, ec);
    } catch (const std::exception& e) { std::printf("EXCEPTION (assets): %s\n", e.what()); return 1; }
    std::printf(g_fail ? "%d check(s) FAILED\n" : "all asset checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
