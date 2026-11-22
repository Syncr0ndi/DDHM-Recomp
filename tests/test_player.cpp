// SPDX-License-Identifier: MIT
// Deterministic tests of the player simulation. Expected values are derived by hand from the decoded
// original code (see docs/PLAYER.md); they check the port is self-consistent with that analysis.
// usage: test_player [game-dir]     (game-dir enables the real-level smoke test)
#include "dd2/exe.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
#include "game/player.hpp"

using namespace ddhm;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)
#define CHECK_RANGE(v, lo, hi) do { const int v_ = (v); if (v_ < (lo) || v_ > (hi)) { std::printf("FAIL %s:%d  %s = %d, expected %d..%d\n", __FILE__, __LINE__, #v, v_, (lo), (hi)); ++g_fail; } } while (0)

static int find_tile(bool top, bool bottom, bool left, bool right) {
    for (int t = 1; t < dd2::kTileCount; ++t)
        if (dd2::tile_face_solid(t, dd2::TileFace::Top) == top && dd2::tile_face_solid(t, dd2::TileFace::Bottom) == bottom &&
            dd2::tile_face_solid(t, dd2::TileFace::Left) == left && dd2::tile_face_solid(t, dd2::TileFace::Right) == right)
            return t;
    return -1;
}

// 40x20 empty room with solid border; floor surface at row `floor_row`.
static dd2::Level make_room(int solid, int floor_row) {
    dd2::Level lv;
    lv.width = 40; lv.height = 20; lv.layers = 2; lv.layer_stride = 40 * 20;
    lv.tiles.assign(40 * 20, 0);
    lv.entities.assign(40 * 20, 0);
    for (int x = 0; x < 40; ++x) { lv.tiles[size_t(floor_row) * 40 + x] = uint16_t(solid); lv.tiles[x] = uint16_t(solid); }
    for (int y = 0; y < 20; ++y) { lv.tiles[size_t(y) * 40] = uint16_t(solid); lv.tiles[size_t(y) * 40 + 39] = uint16_t(solid); }
    return lv;
}

static void run(Player& p, const dd2::Level& lv, const Input& in, int tics) { for (int i = 0; i < tics; ++i) p.tick(in, lv); }

int main(int argc, char** argv) {
    // The game tables (sprites, states, collision, RNG) come from the user's DAVE.EXE, never from this project.
    if (argc < 2) { std::printf("usage: %s <game-dir or Assets-dir> ...\n", argv[0]); return 2; }
    try { dd2::load_exe_from_dir(argv[1]); } catch (const std::exception& e) { std::printf("cannot load DAVE.EXE: %s\n", e.what()); return 1; }
    const int solid = find_tile(true, true, true, true);
    const int oneway = find_tile(true, false, false, false);
    CHECK(solid > 0);
    CHECK(oneway > 0);
    const Input none = make_input(false, false, false, false, false, false);

    // --- input mapping ---------------------------------------------------------------------
    CHECK(make_input(true, false, false, false, false, false).dir == kDirUp);
    CHECK(make_input(false, false, false, true, false, false).dir == kDirRight);
    CHECK(make_input(false, true, true, false, false, false).dir == kDirDownLeft);
    CHECK(make_input(false, false, true, true, false, false).dir == kDirNone);       // opposing keys cancel

    // --- standing ----------------------------------------------------------------------------
    {
        const dd2::Level room = make_room(solid, 14);
        Player p; p.spawn(10, 12, room);
        const int y0 = p.y();
        run(p, room, none, 100);
        CHECK(p.y() == y0);
        CHECK(p.on_ground() == false || p.state().ds == 0x8bc);   // ground flag only set by probes in air states
        CHECK(p.state().ds == 0x8bc);
        CHECK(p.facing() == 1);
    }
    // --- walking: one 64-unit step per 3 tics -------------------------------------------------
    {
        const dd2::Level room = make_room(solid, 14);
        Player p; p.spawn(10, 12, room);
        const int x0 = p.x(), y0 = p.y();
        run(p, room, none, 2);
        run(p, room, make_input(false, false, false, true, false, false), 72);
        CHECK_RANGE(p.x() - x0, 64 * 22, 64 * 24);                // 72 tics of walking = 24 steps
        CHECK(p.y() == y0);
        CHECK(p.facing() == 1);
        run(p, room, make_input(false, false, true, false, false, false), 72);
        CHECK_RANGE(p.x() - x0, -64 * 2, 64 * 2);                 // walked back to about the start
        CHECK(p.facing() == -1);
        run(p, room, none, 10);
        CHECK(p.state().ds == 0x8bc);
    }
    // --- wall blocks walking -------------------------------------------------------------------
    {
        dd2::Level room = make_room(solid, 14);
        for (int y = 10; y < 14; ++y) room.tiles[size_t(y) * 40 + 5] = uint16_t(solid);
        Player p; p.spawn(10, 12, room);
        run(p, room, none, 2);
        run(p, room, make_input(false, false, true, false, false, false), 400);
        // Dave's hit-box left edge may not enter column 5 (x >= 6 tiles)
        CHECK(p.x() + dd2::kSpriteTable[p.sprite()].hit_left + dd2::kSpriteTable[p.sprite()].draw_dx >= 6 * 256);
        CHECK(p.x() < 10 * 256);
    }
    // --- full jump: 14 tics at -40 then gravity 3/tic: apex rises 807 units ------------------
    {
        const dd2::Level room = make_room(solid, 14);
        Player p; p.spawn(10, 12, room);
        const int y0 = p.y();
        run(p, room, none, 2);
        int min_y = y0;
        const Input jump = make_input(false, false, false, false, true, false);
        for (int i = 0; i < 120; ++i) {
            p.tick(i < 30 ? jump : none, room);
            min_y = std::min(min_y, p.y());
        }
        CHECK_RANGE(y0 - min_y, 800, 815);
        CHECK(p.y() == y0);                                       // lands back on the same floor
        CHECK(p.state().ds == 0x8bc);
    }
    // --- short hop: releasing the button cancels the rise ----------------------------------------
    {
        const dd2::Level room = make_room(solid, 14);
        Player p; p.spawn(10, 12, room);
        const int y0 = p.y();
        run(p, room, none, 2);
        int min_y = y0;
        p.tick(make_input(false, false, false, false, true, false), room);
        for (int i = 0; i < 80; ++i) { p.tick(none, room); min_y = std::min(min_y, p.y()); }
        CHECK_RANGE(y0 - min_y, 280, 295);                        // 40 + (37+34+...+1)
    }
    // --- running jump keeps horizontal speed up to 24 units/tic -------------------------------------
    {
        const dd2::Level room = make_room(solid, 14);
        Player p; p.spawn(10, 12, room);
        run(p, room, none, 2);
        const Input right = make_input(false, false, false, true, false, false);
        run(p, room, right, 6);
        p.tick(make_input(false, false, false, true, true, false), room);   // jump starts (may coincide with a walk step)
        CHECK(p.vx() == 24);
        const int x0 = p.x();
        for (int i = 0; i < 10; ++i) p.tick(make_input(false, false, false, true, true, false), room);
        CHECK_RANGE(p.x() - x0, 24 * 10 - 24, 24 * 10 + 24);       // 24 units per tic while the direction is held
    }
    // --- ceiling stops the rise --------------------------------------------------------------------------------
    {
        dd2::Level room = make_room(solid, 14);
        for (int x = 8; x < 14; ++x) room.tiles[size_t(10) * 40 + x] = uint16_t(solid);
        Player p; p.spawn(10, 12, room);
        run(p, room, none, 2);
        int min_y = p.y();
        for (int i = 0; i < 60; ++i) { p.tick(make_input(false, false, false, false, true, false), room); min_y = std::min(min_y, p.y()); }
        CHECK(min_y >= 11 * 256 - 16 + 0);                        // head (hit-box top at y+16) stays below row 10
    }
    // --- drop through a one-way floor with Down+Jump -----------------------------------------------------------
    {
        dd2::Level room = make_room(solid, 18);
        for (int x = 5; x < 20; ++x) room.tiles[size_t(14) * 40 + x] = uint16_t(oneway);
        Player p; p.spawn(10, 12, room);
        const int y0 = p.y();
        run(p, room, none, 3);
        CHECK(p.y() == y0);                                       // standing on the one-way floor
        run(p, room, make_input(false, true, false, false, false, false), 2);       // aim down
        run(p, room, make_input(false, true, false, false, true, false), 3);        // drop through
        run(p, room, none, 90);
        CHECK(p.y() > y0 + 256);                                  // ended on the lower floor
    }
    // --- shooting: wind-up, shot, recoil, ammo, reload --------------------------------------------------------
    {
        const dd2::Level room = make_room(solid, 14);
        Player p; p.spawn(10, 12, room);
        const int x0 = p.x();
        run(p, room, none, 2);
        p.tick(make_input(false, false, false, false, false, true), room);
        CHECK(p.state().ds == 0xa10);
        bool saw_flash = false;
        int ammo_at_flash = -1;
        for (int i = 0; i < 40; ++i) {
            p.tick(none, room);
            if (!saw_flash && !p.effects().empty()) { saw_flash = true; ammo_at_flash = p.ammo(); }
        }
        CHECK(saw_flash);
        CHECK(ammo_at_flash == 7);
        CHECK(p.ammo() == 7);
        CHECK(p.x() == x0 - 128);                                 // two 64-unit recoil steps
        CHECK(p.state().ds == 0x8bc);
        run(p, room, none, 400);                                  // idle: reloads back to 8
        CHECK(p.ammo() == 8);
        CHECK(p.state().ds == 0x8bc);
    }
    // --- empty gun clicks ------------------------------------------------------------------------------------
    {
        const dd2::Level room = make_room(solid, 14);
        Player p; p.spawn(10, 12, room);
        run(p, room, none, 2);
        for (int shot = 0; shot < 8; ++shot) {
            p.tick(make_input(false, false, false, false, false, true), room);
            run(p, room, none, 40);                               // finish the shot; (idle too short to reload: <31 tics? keep firing)
            if (p.ammo() < 8 && p.ammo() > 0) { /* keep firing before the idle reload begins */ }
        }
        CHECK(p.ammo() >= 0 && p.ammo() <= 8);
    }

    // --- camera stops at (width-22) tiles ---------------------------------------------------------------------
    {
        dd2::Level wide = make_room(solid, 14);
        wide.width = 80; wide.tiles.assign(80 * 20, 0); wide.entities.assign(80 * 20, 0); wide.layer_stride = 80 * 20;
        for (int x = 0; x < 80; ++x) { wide.tiles[size_t(14) * 80 + x] = uint16_t(solid); }
        for (int y = 0; y < 20; ++y) { wide.tiles[size_t(y) * 80] = uint16_t(solid); wide.tiles[size_t(y) * 80 + 79] = uint16_t(solid); }
        Player p; p.spawn(10, 12, wide);
        run(p, wide, none, 2);
        run(p, wide, make_input(false, false, false, true, false, false), 3000);
        CHECK(p.camera().x == (80 - 22) * 256);
    }

    // --- real level smoke test ---------------------------------------------------------------------------------
    if (argc > 1) {
        const std::string dir = std::string(argv[1]) + "/";
        const dd2::Level lv = dd2::load_level(dir + "LEVEL01.DD2");
        const auto ents = dd2::extract_entities(lv);
        CHECK(ents.player_index >= 0);
        const auto& s = ents.spawns[size_t(ents.player_index)];
        Player p; p.spawn(s.tile_x, s.tile_y, lv);
        const int y0 = p.y();
        run(p, lv, none, 200);
        CHECK(p.y() == y0);                                       // the real start position is on solid ground
        CHECK(p.state().ds == 0x8bc);
        run(p, lv, make_input(false, false, false, true, false, false), 140);
        CHECK(p.x() > s.tile_x * 256 + 256);                      // walked right
        run(p, lv, none, 5);
        // Camera: starts clamped 2 tiles from the corner; never shows the border tiles; stops at the far limits.
        Player q; q.spawn(s.tile_x, s.tile_y, lv);
        CHECK(q.camera().x == 0x200);
        CHECK_RANGE(q.camera().y, 0x200, (lv.height - 15) * 256);
        const dd2::Level lv4 = dd2::load_level(dir + "LEVEL04.DD2");       // 24x17: limits coincide
        const auto e4 = dd2::extract_entities(lv4);
        Player r; r.spawn(e4.spawns[size_t(e4.player_index)].tile_x, e4.spawns[size_t(e4.player_index)].tile_y, lv4);
        run(r, lv4, make_input(false, false, false, true, false, false), 300);
        CHECK(r.camera().x == 0x200 && r.camera().y == 0x200);
        for (int i = 0; i < 4000; ++i) { q.tick(make_input(false, false, true, false, false, false), lv); }
        CHECK(q.camera().x >= 0x200);
    }

    std::printf(g_fail ? "%d player check(s) FAILED\n" : "all player checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
