// SPDX-License-Identifier: MIT
// playtrace <game-dir> <out.png>
// Runs a scripted sequence on Level 1 (walk, running jump, shoot) and draws Dave every few tics, so the
// movement model and sprite placement can be inspected visually. No SDL needed.
#include "dd2/exe.hpp"
#include <cstdio>
#include <string>
#include "game/player.hpp"

using namespace ddhm;

int main(int argc, char** argv) {
    // The game tables (sprites, states, collision, RNG) come from the user's DAVE.EXE, never from this project.
    if (argc < 2) { std::printf("usage: %s <game-dir or Assets-dir> ...\n", argv[0]); return 2; }
    try { dd2::load_exe_from_dir(argv[1]); } catch (const std::exception& e) { std::printf("cannot load DAVE.EXE: %s\n", e.what()); return 1; }
    if (argc < 3) { std::fprintf(stderr, "usage: %s <game-dir> <out.png>\n", argv[0]); return 2; }
    const std::string dir = std::string(argv[1]) + "/";
    const dd2::TileSet tiles = dd2::load_tileset(dir + "EGATILES.DD2");
    const dd2::Level lv = dd2::load_level(dir + "LEVEL01.DD2");
    const auto sprites = dd2::load_sprites(dir);
    const auto ents = dd2::extract_entities(lv);
    auto img = dd2::render_level_rgba(lv, tiles);
    const int W = lv.width * 16, H = lv.height * 16;

    const auto& s0 = ents.spawns[size_t(ents.player_index)];
    Player p;
    p.spawn(s0.tile_x, s0.tile_y, lv);
    auto draw = [&](bool mark) {
        dd2::blit_sprite_rgba(img, W, H, sprites[size_t(p.sprite())], p.draw_x(), p.draw_y());
        for (const Effect& e : p.effects()) {
            const int spr = e.facing > 0 ? e.state->sprite_e : e.state->sprite_w;
            dd2::blit_sprite_rgba(img, W, H, sprites[size_t(spr)], (e.x + dd2::kSpriteTable[spr].draw_dx) >> 4,
                                  (e.y + dd2::kSpriteTable[spr].draw_dy) >> 4);
        }
        (void)mark;
    };
    auto run = [&](const Input& in, int tics, int every) {
        for (int i = 0; i < tics; ++i) { p.tick(in, lv); if (every && i % every == 0) draw(false); }
    };
    const Input none = make_input(false, false, false, false, false, false);
    const Input right = make_input(false, false, false, true, false, false);
    const Input rjump = make_input(false, false, false, true, true, false);
    run(none, 3, 0);
    run(right, 40, 8);
    run(rjump, 14, 3);
    run(right, 60, 6);                       // fall to the ground, still moving right
    run(none, 20, 0);
    p.tick(make_input(false, false, false, false, false, true), lv);
    run(none, 40, 6);                        // shot: flash, smoke, recoil
    draw(true);

    // crop around the start area
    const int cx0 = 0, cy0 = (s0.tile_y - 8) * 16, cw = 480, ch = 192;
    std::vector<uint8_t> crop(size_t(cw) * ch * 4);
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x)
            for (int c = 0; c < 4; ++c) crop[(size_t(y) * cw + x) * 4 + c] = img[((size_t(cy0) + y) * W + cx0 + x) * 4 + c];
    dd2::write_png_rgba(argv[2], cw, ch, crop.data());
    std::printf("final: x=%d y=%d state=0x%x ammo=%d on_ground=%d\n", p.x(), p.y(), p.state().ds, p.ammo(), int(p.on_ground()));
    return 0;
}
