// SPDX-License-Identifier: MIT
// dd2tool <game-dir> <out-dir>
// Exports every decodable asset to PNG so the formats can be inspected visually.
#include "dd2/exe.hpp"
#include <cstdio>
#include <filesystem>
#include <string>
#include "dd2/dd2.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    // The game tables (sprites, states, collision, RNG) come from the user's DAVE.EXE, never from this project.
    if (argc < 2) { std::printf("usage: %s <game-dir or Assets-dir> ...\n", argv[0]); return 2; }
    try { dd2::load_exe_from_dir(argv[1]); } catch (const std::exception& e) { std::printf("cannot load DAVE.EXE: %s\n", e.what()); return 1; }
    if (argc < 3) { std::fprintf(stderr, "usage: %s <game-dir> <out-dir>\n", argv[0]); return 2; }
    const fs::path in = argv[1], out = argv[2];
    fs::create_directories(out);
    int failures = 0;
    try {
        const dd2::TileSet ts = dd2::load_tileset((in / "EGATILES.DD2").string());
        const int cols = 32, rows = (ts.count + cols - 1) / cols;
        std::vector<uint8_t> atlas(size_t(cols) * 16 * rows * 16 * 4, 0);
        for (int i = 0; i < ts.count; ++i)
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    const dd2::Rgb c = dd2::kEgaPalette[ts.tile(i)[y * 16 + x]];
                    uint8_t* o = &atlas[((size_t(i / cols) * 16 + y) * cols * 16 + (i % cols) * 16 + x) * 4];
                    o[0] = c.r; o[1] = c.g; o[2] = c.b; o[3] = 255;
                }
        dd2::write_png_rgba((out / "tiles.png").string(), cols * 16, rows * 16, atlas.data());
        std::printf("tiles.png            %d tiles\n", ts.count);

        for (int n = 1; n <= 8; ++n) {
            char name[32];
            std::snprintf(name, sizeof name, "LEVEL%02d.DD2", n);
            try {
                const dd2::Level lv = dd2::load_level((in / name).string());
                const auto img = dd2::render_level_rgba(lv, ts);
                std::snprintf(name, sizeof name, "level%02d.png", n);
                dd2::write_png_rgba((out / name).string(), lv.width * 16, lv.height * 16, img.data());
                std::printf("%-20s %dx%d tiles\n", name, lv.width, lv.height);
            } catch (const std::exception& e) { std::printf("LEVEL%02d: FAILED: %s\n", n, e.what()); ++failures; }
        }
        for (const char* pic : {"TITLE1", "TITLE2", "STARPIC", "PROGPIC"}) {
            try {
                const dd2::Picture p = dd2::load_picture((in / (std::string(pic) + ".DD2")).string());
                const auto img = dd2::picture_to_rgba(p);
                std::string o = std::string(pic) + ".png";
                for (auto& ch : o) ch = char(std::tolower(ch));
                dd2::write_png_rgba((out / o).string(), p.width, p.height, img.data());
                std::printf("%-20s %dx%d\n", o.c_str(), p.width, p.height);
            } catch (const std::exception& e) { std::printf("%s: FAILED: %s\n", pic, e.what()); ++failures; }
        }
        try {
            const dd2::Picture p = dd2::load_intro((in / "INTRO.DD2").string());
            const auto img = dd2::picture_to_rgba(p);
            dd2::write_png_rgba((out / "intro.png").string(), p.width, p.height, img.data());
            std::printf("%-20s %dx%d\n", "intro.png", p.width, p.height);
        } catch (const std::exception& e) { std::printf("INTRO: FAILED: %s\n", e.what()); ++failures; }
        try {
            const auto sprites = dd2::load_sprites((in.string() + "/"));
            for (int f = 0; f < dd2::kSpriteFileCount; ++f) {
                const int cell = 48, cols = 12;
                int n = 0;
                for (auto& s : sprites) if (s.info->file == f) ++n;
                const int rows = (n + cols - 1) / cols, W = cols * cell, H = rows * cell;
                std::vector<uint8_t> sheet(size_t(W) * H * 4);
                for (size_t i = 0; i < sheet.size(); i += 4) {           // checkerboard background
                    const size_t px = i / 4;
                    const uint8_t v = (((px % W) / 8 + (px / W) / 8) & 1) ? 60 : 90;
                    sheet[i] = sheet[i + 1] = sheet[i + 2] = v; sheet[i + 3] = 255;
                }
                int k = 0;
                for (auto& s : sprites) {
                    if (s.info->file != f) continue;
                    const auto rgba = dd2::sprite_to_rgba(s);
                    const int ox = (k % cols) * cell, oy = (k / cols) * cell;
                    for (int y = 0; y < s.height && y < cell; ++y)
                        for (int x = 0; x < s.width && x < cell; ++x) {
                            const uint8_t* p = &rgba[(size_t(y) * s.width + x) * 4];
                            if (!p[3]) continue;
                            uint8_t* o = &sheet[((size_t(oy) + y) * W + ox + x) * 4];
                            o[0] = p[0]; o[1] = p[1]; o[2] = p[2];
                        }
                    ++k;
                }
                const std::string name = std::string("sprites_") + dd2::kSpriteFileNames[f] + ".png";
                dd2::write_png_rgba((out / name).string(), W, H, sheet.data());
                std::printf("%-20s %d sprites\n", name.c_str(), n);
            }
        } catch (const std::exception& e) { std::printf("sprites: FAILED: %s\n", e.what()); ++failures; }
        try {
            const auto sprites = dd2::load_sprites(in.string() + "/");
            for (int n = 1; n <= 8; ++n) {
                char name[32];
                std::snprintf(name, sizeof name, "LEVEL%02d.DD2", n);
                const dd2::Level lv = dd2::load_level((in / name).string());
                auto img = dd2::render_level_rgba(lv, ts);
                const auto ents = dd2::extract_entities(lv);
                for (const auto& s : ents.spawns)
                    if (s.sprite) dd2::blit_sprite_rgba(img, lv.width * 16, lv.height * 16, sprites[size_t(s.sprite)], s.px, s.py);
                std::snprintf(name, sizeof name, "level%02d_entities.png", n);
                dd2::write_png_rgba((out / name).string(), lv.width * 16, lv.height * 16, img.data());
                std::printf("%-26s %zu spawns, %zu triggers\n", name, ents.spawns.size(), ents.triggers.size());
            }
        } catch (const std::exception& e) { std::printf("entities: FAILED: %s\n", e.what()); ++failures; }
        for (int n = 1; n <= 8; ++n) {
            char name[40];
            std::snprintf(name, sizeof name, "LEVEL%02d.DD2", n);
            try {
                const dd2::Level lv = dd2::load_level((in / name).string());
                auto img = dd2::render_level_rgba(lv, ts);
                const auto ov = dd2::render_collision_overlay_rgba(lv);
                for (size_t i = 0; i < img.size(); i += 4) {
                    const unsigned a = ov[i + 3];
                    if (!a) continue;
                    for (int c = 0; c < 3; ++c) img[i + c] = uint8_t((img[i + c] * (255 - a) + ov[i + c] * a) / 255);
                }
                std::snprintf(name, sizeof name, "level%02d_collision.png", n);
                dd2::write_png_rgba((out / name).string(), lv.width * 16, lv.height * 16, img.data());
                std::printf("%-26s collision overlay\n", name);
            } catch (const std::exception& e) { std::printf("collision LEVEL%02d: FAILED: %s\n", n, e.what()); ++failures; }
        }
        for (const char* s : {"S_DAVE", "S_FRANK", "S_MASTER", "S_CHUNK1", "S_CHUNK2"}) {
            try {
                const auto raw = dd2::huff_expand(dd2::read_file((in / (std::string(s) + ".DD2")).string()));
                std::string o = std::string(s) + ".bin";
                FILE* f = std::fopen((out / o).string().c_str(), "wb");
                if (f) { std::fwrite(raw.data(), 1, raw.size(), f); std::fclose(f); }
                std::printf("%-20s %zu bytes (layout not yet decoded)\n", o.c_str(), raw.size());
            } catch (const std::exception& e) { std::printf("%s: FAILED: %s\n", s, e.what()); ++failures; }
        }
    } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
    return failures ? 1 : 0;
}
