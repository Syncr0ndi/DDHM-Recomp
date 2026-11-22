// SPDX-License-Identifier: MIT
// zootrace <game-dir> <out.png> [tics]
// A synthetic room with one of every ported enemy, drawn with the real tiles and sprites after a number of tics.
// Used to check the new AI visually. No SDL needed.
#include "dd2/exe.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include "game/world.hpp"

using namespace ddhm;

int main(int argc, char** argv) {
    // The game tables (sprites, states, collision, RNG) come from the user's DAVE.EXE, never from this project.
    if (argc < 2) { std::printf("usage: %s <game-dir or Assets-dir> ...\n", argv[0]); return 2; }
    try { dd2::load_exe_from_dir(argv[1]); } catch (const std::exception& e) { std::printf("cannot load DAVE.EXE: %s\n", e.what()); return 1; }
    if (argc < 3) { std::fprintf(stderr, "usage: %s <game-dir> <out.png> [tics]\n", argv[0]); return 2; }
    const std::string dir = std::string(argv[1]) + "/";
    const int total = argc > 3 ? std::atoi(argv[3]) : 60;
    const dd2::TileSet tiles = dd2::load_tileset(dir + "EGATILES.DD2");
    const auto sprites = dd2::load_sprites(dir);
    int solid = -1;
    for (int t = 1; t < dd2::kTileCount && solid < 0; ++t)
        if (dd2::tile_face_solid(t, dd2::TileFace::Top) && dd2::tile_face_solid(t, dd2::TileFace::Bottom) &&
            dd2::tile_face_solid(t, dd2::TileFace::Left) && dd2::tile_face_solid(t, dd2::TileFace::Right)) solid = t;

    dd2::Level lv;
    lv.width = 60; lv.height = 20; lv.layers = 2; lv.layer_stride = 1200;
    lv.tiles.assign(1200, 0); lv.entities.assign(1200, 0);
    for (int x = 0; x < 60; ++x) { lv.tiles[size_t(14) * 60 + x] = uint16_t(solid); lv.tiles[x] = uint16_t(solid); }
    for (int y = 0; y < 20; ++y) { lv.tiles[size_t(y) * 60] = uint16_t(solid); lv.tiles[size_t(y) * 60 + 59] = uint16_t(solid); }
    for (int x = 20; x < 40; ++x) lv.tiles[size_t(7) * 60 + x] = uint16_t(solid);            // ceiling for the spider
    auto put = [&](int x, int y, int code) { lv.entities[size_t(y) * 60 + x] = uint16_t(code); };
    const bool master = argc > 4 && std::string(argv[4]) == "master";
    const bool frank = argc > 4 && std::string(argv[4]) == "frank";
    if (master) {                                             // the Master, Delbert and Dave
        for (int x = 20; x < 40; ++x) lv.tiles[size_t(7) * 60 + x] = 0;
        put(3, 12, 0xFF);
        put(14, 3, 0x0C);
        put(20, 8, 0x0D);
    } else if (frank) {                                              // Frank in a walled room, Dave outside it
        for (int x = 20; x < 40; ++x) lv.tiles[size_t(7) * 60 + x] = 0;
        for (int y = 0; y < 14; ++y) { lv.tiles[size_t(y) * 60 + 26] = uint16_t(solid); lv.tiles[size_t(y) * 60 + 38] = uint16_t(solid); }
        put(40, 12, 0xFF);
        put(30, 9, 0x0B);
    } else {
    put(30, 12, 0xFF);   // Dave
    put(30, 8, 0x06);    // spider right above him
    put(24, 13, 0x03);   // slime
    put(36, 12, 0x01);   // zombie
    put(20, 12, 0x09);   // wolf
    put(26, 11, 0x07);   // flaming skull
    put(34, 12, 0x04);   // ghost
    }

    World w; w.new_game(); w.load(lv, master ? 8 : 0);
    const Input none = make_input(false, false, false, false, false, false);
    for (int t = 0; t < total && !w.dead(); ++t) w.tick(none);
    std::printf("tics=%d dead=%d state=%d\n", total, int(w.dead()), w.level_state());

    auto img = dd2::render_level_rgba(lv, tiles);
    const int W = lv.width * 16, H = lv.height * 16;
    auto draw = [&](int spr, int px, int py) { if (spr > 0) dd2::blit_sprite_rgba(img, W, H, sprites[size_t(spr)], px, py); };
    std::vector<ExtraDraw> ex;
    for (const Enemy& e : w.objects()) {
        if (!e.kind) continue;
        draw(e.sprite(), e.draw_x(), e.draw_y());
        ex.clear(); e.extra_draws(ex);
        for (const auto& x : ex) draw(x.sprite, (x.x + dd2::kSpriteTable[x.sprite].draw_dx) >> 4, (x.y + dd2::kSpriteTable[x.sprite].draw_dy) >> 4);
    }
    for (const Effect& f : w.player().effects()) {
        const int s = f.facing > 0 ? f.state->sprite_e : f.state->sprite_w;
        draw(s, (f.x + dd2::kSpriteTable[s].draw_dx) >> 4, (f.y + dd2::kSpriteTable[s].draw_dy) >> 4);
    }
    if (!w.dead()) draw(w.player().sprite(), w.player().draw_x(), w.player().draw_y());
    const int cx0 = (master ? 0 : frank ? 22 : 15) * 16, cy0 = (master ? 0 : frank ? 3 : 6) * 16, cw = 480, ch = (master || frank) ? 240 : 176;
    std::vector<uint8_t> crop(size_t(cw) * ch * 4);
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x)
            for (int c = 0; c < 4; ++c) crop[(size_t(y) * cw + x) * 4 + c] = img[((size_t(cy0) + y) * W + cx0 + x) * 4 + c];
    dd2::write_png_rgba(argv[2], cw, ch, crop.data());
    return 0;
}
