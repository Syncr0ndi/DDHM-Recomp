// SPDX-License-Identifier: MIT
// enemytrace <game-dir> <out.png> [tics]
// A tiny bot plays Level 1 (walks right, shoots enemies ahead of it) and the final state is rendered, so the
// enemy behaviour (walking, hits, gibs, score popups, knives) can be inspected visually. No SDL needed.
#include "dd2/exe.hpp"
#include <algorithm>
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
    const int total = argc > 3 ? std::atoi(argv[3]) : 1500;
    const dd2::TileSet tiles = dd2::load_tileset(dir + "EGATILES.DD2");
    const dd2::Level lv = dd2::load_level(dir + "LEVEL01.DD2");
    const auto sprites = dd2::load_sprites(dir);

    World w;
    w.new_game();
    w.load(lv);
    w.player().spawn(24, 33, lv);                  // start on the mansion's ground floor, where the zombies are
    int kills_seen = 0, last_score = 0;
    int min_x = w.player().x();
    int last_x = -1, stuck = 0;
    for (int t = 0; t < total && !w.dead(); ++t) {
        // bot: shoot when an active shootable enemy is within 14 tiles ahead on roughly the same level
        bool target = false;
        for (const Enemy& e : w.objects()) {
            if (!e.kind || !e.active || !e.shootable) continue;
            const int dx = e.x() - w.player().x(), dy = e.y() - w.player().y();
            if (dx > 0 && dx < 14 * 256 && std::abs(dy) < 0x180) target = true;
        }
        const bool fire = target && (t % 50 == 0);
        const bool walk = !target;
        stuck = (w.player().x() == last_x) ? stuck + 1 : 0;      // blocked by scenery: jump over it
        last_x = w.player().x();
        const bool jump = walk && stuck > 6;
        w.tick(make_input(false, false, false, walk, jump, fire));
        if (int(w.score()) != last_score) { ++kills_seen; last_score = int(w.score()); std::printf("t=%d score=%d\n", t, last_score); }
        min_x = std::min(min_x, w.player().x());
    }
    std::printf("end: tics ok, dead=%d state=%d score=%u lives=%d player tile x=%d kills=%d\n", int(w.dead()), w.level_state(),
                w.score(), w.lives(), w.player().x() >> 8, kills_seen);

    auto img = dd2::render_level_rgba(lv, tiles);
    const int W = lv.width * 16, H = lv.height * 16;
    auto draw = [&](int spr, int px, int py) { if (spr > 0) dd2::blit_sprite_rgba(img, W, H, sprites[size_t(spr)], px, py); };
    for (const Enemy& e : w.objects()) if (e.kind) draw(e.sprite(), e.draw_x(), e.draw_y());
    for (const Effect& f : w.player().effects()) {
        const int s = f.facing > 0 ? f.state->sprite_e : f.state->sprite_w;
        draw(s, (f.x + dd2::kSpriteTable[s].draw_dx) >> 4, (f.y + dd2::kSpriteTable[s].draw_dy) >> 4);
    }
    draw(w.player().sprite(), w.player().draw_x(), w.player().draw_y());

    const int cw = 480, ch = 240;
    int cx0 = std::max(0, (w.player().x() >> 4) - 200), cy0 = std::max(0, (w.player().y() >> 4) - 150);
    cx0 = std::min(cx0, W - cw); cy0 = std::min(cy0, H - ch);
    std::vector<uint8_t> crop(size_t(cw) * ch * 4);
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x)
            for (int c = 0; c < 4; ++c) crop[(size_t(y) * cw + x) * 4 + c] = img[((size_t(cy0) + y) * W + cx0 + x) * 4 + c];
    dd2::write_png_rgba(argv[2], cw, ch, crop.data());
    return 0;
}
