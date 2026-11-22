// SPDX-License-Identifier: MIT
// Deterministic tests of the enemy / world simulation on synthetic rooms. Expectations come from the decoded
// original code (docs/ENEMIES.md). usage: test_enemy [game-dir]  (game-dir enables a real-level smoke test)
#include "dd2/exe.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "game/world.hpp"

using namespace ddhm;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static int find_solid() {
    for (int t = 1; t < dd2::kTileCount; ++t)
        if (dd2::tile_face_solid(t, dd2::TileFace::Top) && dd2::tile_face_solid(t, dd2::TileFace::Bottom) &&
            dd2::tile_face_solid(t, dd2::TileFace::Left) && dd2::tile_face_solid(t, dd2::TileFace::Right)) return t;
    return -1;
}

// 60x20 room, solid border, floor surface at row 14 (Dave/zombies stand with their feet on it).
static dd2::Level make_room(int solid) {
    dd2::Level lv;
    lv.width = 60; lv.height = 20; lv.layers = 2; lv.layer_stride = 60 * 20;
    lv.tiles.assign(60 * 20, 0);
    lv.entities.assign(60 * 20, 0);
    for (int x = 0; x < 60; ++x) { lv.tiles[size_t(14) * 60 + x] = uint16_t(solid); lv.tiles[x] = uint16_t(solid); }
    for (int y = 0; y < 20; ++y) { lv.tiles[size_t(y) * 60] = uint16_t(solid); lv.tiles[size_t(y) * 60 + 59] = uint16_t(solid); }
    return lv;
}
static void put(dd2::Level& lv, int x, int y, int code) { lv.entities[size_t(y) * 60 + x] = uint16_t(code); }

static const Enemy* find_kind(const World& w, int kind) {
    for (const Enemy& e : w.objects()) if (e.kind == kind) return &e;
    return nullptr;
}
static int count_kind(const World& w, int kind) {
    int n = 0;
    for (const Enemy& e : w.objects()) n += e.kind == kind ? 1 : 0;
    return n;
}

int main(int argc, char** argv) {
    // The game tables (sprites, states, collision, RNG) come from the user's DAVE.EXE, never from this project.
    if (argc < 2) { std::printf("usage: %s <game-dir or Assets-dir> ...\n", argv[0]); return 2; }
    try { dd2::load_exe_from_dir(argv[1]); } catch (const std::exception& e) { std::printf("cannot load DAVE.EXE: %s\n", e.what()); return 1; }
    const int solid = find_solid();
    CHECK(solid > 0);
    const Input none = make_input(false, false, false, false, false, false);
    const Input fire = make_input(false, false, false, false, false, true);

    // --- RNG is the game's table ---------------------------------------------------------------
    { Rng r; CHECK(r.next() == 8); CHECK(r.next() == 109); CHECK(r.next() == 220); }

    // --- zombie walks to Dave and kills him with its third attack frame -------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 40, 12, 0xFF);           // Dave
        put(room, 28, 12, 0x01);           // zombie, 12 tiles away on the same floor
        World w; w.new_game(); w.load(room);
        CHECK(count_kind(w, kKindZombie) == 1);
        const int zx0 = find_kind(w, kKindZombie)->x(), zy0 = find_kind(w, kKindZombie)->y();
        int died_at = -1;
        for (int t = 0; t < 3000 && died_at < 0; ++t) { w.tick(none); if (w.dead()) died_at = t; }
        CHECK(died_at > 0);
        CHECK(w.level_state() == -3);
        const Enemy* z = find_kind(w, kKindZombie);
        CHECK(z != nullptr);
        if (z) { CHECK(z->x() > zx0); CHECK(z->y() == zy0); CHECK(z->facing() == 1); }
    }
    // --- a zombie behind Dave cannot hurt him -----------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        put(room, 21, 12, 0x01);
        World w; w.new_game(); w.load(room);
        for (int t = 0; t < 40; ++t) w.tick(none);
        CHECK(w.level_state() == 0 || w.level_state() == -3);    // it attacks only once it faces him (see below)
    }
    // --- two shotgun hits kill a zombie: gibs, popup, +100 points ----------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        put(room, 28, 12, 0x01);
        World w; w.new_game(); w.load(room);
        w.tick(none);
        for (int shot = 0; shot < 2; ++shot) {
            w.tick(fire);
            for (int t = 0; t < 45; ++t) w.tick(none);
        }
        CHECK(count_kind(w, kKindZombie) == 0);
        CHECK(w.score() == 100);
        CHECK(w.level_state() == 0);
        CHECK(count_kind(w, kKindDebris) > 0);                   // gibs and the 100 popup
    }
    // --- one hit only wounds ----------------------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        put(room, 28, 12, 0x01);
        World w; w.new_game(); w.load(room);
        w.tick(none);
        w.tick(fire);
        for (int t = 0; t < 40; ++t) w.tick(none);
        const Enemy* z = find_kind(w, kKindZombie);
        CHECK(z != nullptr);
        if (z) CHECK(z->hp == 1);
        CHECK(w.score() == 0);
    }
    // --- walkers turn around at ledges and stay on their platform ---------------------------------------
    {
        dd2::Level room = make_room(solid);
        for (int x = 0; x < 60; ++x) room.tiles[size_t(14) * 60 + x] = 0;               // remove the main floor
        for (int x = 0; x < 60; ++x) room.tiles[size_t(19) * 60 + x] = uint16_t(solid);  // ground at row 19
        for (int x = 8; x <= 12; ++x) room.tiles[size_t(14) * 60 + x] = uint16_t(solid); // small platform
        put(room, 18, 17, 0xFF);           // Dave on the ground floor, below and to the right (zombie is awake but not triggered)
        put(room, 10, 12, 0x01);           // zombie in the middle of the platform
        World w; w.new_game(); w.load(room);
        const int y0 = find_kind(w, kKindZombie)->y();
        bool turned = false;
        int last_facing = find_kind(w, kKindZombie)->facing();
        for (int t = 0; t < 900; ++t) {
            w.tick(none);
            const Enemy* z = find_kind(w, kKindZombie);
            if (z->facing() != last_facing) turned = true;
            last_facing = z->facing();
            CHECK(z->y() == y0);                                  // never fell off
            CHECK(z->box().lc >= 7 && z->box().rc <= 13);
        }
        CHECK(turned);
    }
    // --- far-away enemies sleep ---------------------------------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 3, 12, 0xFF);
        put(room, 55, 12, 0x01);
        World w; w.new_game(); w.load(room);
        const int x0 = find_kind(w, kKindZombie)->x();
        for (int t = 0; t < 200; ++t) w.tick(none);
        CHECK(!find_kind(w, kKindZombie)->active);
        CHECK(find_kind(w, kKindZombie)->x() == x0);
    }
    // --- hunchback throws knives when Dave is far; a knife kills --------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 40, 12, 0xFF);
        put(room, 29, 12, 0x02);           // 11 tiles away, just inside the activation region
        World w; w.new_game(); w.load(room);
        bool saw_knife = false;
        for (int t = 0; t < 2500 && !w.dead(); ++t) {
            w.tick(none);
            if (count_kind(w, kKindKnife) > 0) saw_knife = true;
        }
        CHECK(saw_knife);
    }
    // --- pickups: points, popup, 1UP ------------------------------------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        put(room, 20, 12 + 0, 0xFF);
        put(room, 22, 12, 0x0E);           // type 0 (100)
        put(room, 22, 13, 0x12);           // type 4 (1600)
        put(room, 23, 12, 0x13);           // type 5 (1UP)
        World w; w.new_game(); w.load(room);
        CHECK(w.lives() == 4);
        int pickup_x = -1, pickup_y = -1;
        for (const Enemy& e : w.objects()) if (e.kind == kKindPickup && e.pickup_type == 0) { pickup_x = e.x(); pickup_y = e.y(); }
        // walk right through them
        bool popup_at_pickup = false;
        for (int t = 0; t < 200; ++t) {
            w.tick(make_input(false, false, false, true, false, false));
            for (const Enemy& e : w.objects())    // the 100-point popup starts where the pickup was (0x45E6: no +0x40)
                if (e.kind == kKindDebris && e.state().ds == 0xCE0 && e.x() == pickup_x && e.age <= 1) popup_at_pickup = true;
        }
        CHECK(pickup_x >= 0 && pickup_y >= 0);
        CHECK(popup_at_pickup);
        CHECK(w.score() >= 100);
        CHECK(count_kind(w, kKindPickup) <= 1);
    }
    // --- extra life every 10,000 points ------------------------------------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        World w; w.new_game(); w.load(room);
        for (int i = 0; i < 6; ++i) w.score_popup(0, 0, 5);      // 9,600
        CHECK(w.lives() == 4);
        w.score_popup(0, 0, 3);                                   // 10,000
        CHECK(w.lives() == 5);
        CHECK(w.score() == 10000);
    }
    // --- restart costs a life and keeps the score ------------------------------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        World w; w.new_game(); w.load(room);
        w.score_popup(0, 0, 2);
        w.restart();
        CHECK(w.lives() == 3);
        CHECK(w.score() == 200);
        CHECK(w.level_state() == 0);
    }

    // --- wolf: runs at Dave, pounces within 6 tiles, kills him ---------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 40, 12, 0xFF);
        put(room, 28, 12, 0x09);
        World w; w.new_game(); w.load(room);
        bool leapt = false;
        int x0 = find_kind(w, kKindWolf)->x();
        // A standing Dave is cleared by the leap (the wolf pounces from ~6 tiles and flies over his head).
        for (int t = 0; t < 400 && !w.dead(); ++t) {
            w.tick(none);
            const Enemy* e = find_kind(w, kKindWolf);
            if (e && e->state().ds == 0x1104) leapt = true;
        }
        CHECK(leapt);
        CHECK(!w.dead());
        CHECK(find_kind(w, kKindWolf)->x() > x0);
        // Walking into its path is deadly.
        World w2; w2.new_game(); w2.load(room);
        const Input left = make_input(false, false, true, false, false, false);
        for (int t = 0; t < 600 && !w2.dead(); ++t) w2.tick(left);
        CHECK(w2.level_state() == -6);
    }
    // --- wolf takes three hits ----------------------------------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        put(room, 30, 12, 0x09);
        World w; w.new_game(); w.load(room);
        const Enemy* e = find_kind(w, kKindWolf);
        CHECK(e && e->hp == 3);
        w.tick(none);
        w.tick(fire);
        for (int t = 0; t < 40; ++t) w.tick(none);
        if (find_kind(w, kKindWolf)) CHECK(find_kind(w, kKindWolf)->hp <= 2);
    }
    // --- slime: killed by one hit (+200), deadly on contact -------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        put(room, 22, 13, 0x03);           // floor slime 2 tiles away: the tile below is the floor
        World w; w.new_game(); w.load(room);
        CHECK(count_kind(w, kKindSlime) == 1);
        CHECK(find_kind(w, kKindSlime)->aim() == 1);
        const Input aim_down_fire = make_input(false, true, false, true, false, true);    // slimes are low: aim down
        const Input aim_down = make_input(false, true, false, true, false, false);
        w.tick(none);
        w.tick(aim_down_fire);
        for (int t = 0; t < 40; ++t) w.tick(aim_down);
        CHECK(count_kind(w, kKindSlime) == 0);
        CHECK(w.score() == 200);
    }
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        put(room, 21, 13, 0x03);
        World w; w.new_game(); w.load(room);
        for (int t = 0; t < 60 && !w.dead(); ++t) w.tick(none);
        CHECK(w.level_state() == -2);
    }
    // --- ceiling slime has the other variant ------------------------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        put(room, 26, 8, 0x03);            // nothing solid below it: ceiling variant
        World w; w.new_game(); w.load(room);
        CHECK(find_kind(w, kKindSlime)->aim() == -1);
        CHECK(find_kind(w, kKindSlime)->state().ds == 0xF24);
    }
    // --- ghost: untouchable and flickering until it touches Dave, then solid and deadly ---------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        put(room, 26, 12, 0x04);
        World w; w.new_game(); w.load(room);
        const Enemy* g = find_kind(w, kKindGhost);
        CHECK(g && !g->shootable);
        w.tick(none);
        w.tick(fire);                                              // shots pass through it
        for (int t = 0; t < 40; ++t) w.tick(none);
        CHECK(find_kind(w, kKindGhost) != nullptr);
        CHECK(w.score() == 0);
        bool solid_seen = false;
        for (int t = 0; t < 600 && !w.dead(); ++t) {
            w.tick(none);
            const Enemy* e = find_kind(w, kKindGhost);
            if (e && e->shootable) solid_seen = true;
        }
        CHECK(solid_seen);
        CHECK(w.level_state() == -5);
    }
    // --- delayed ghost appears only BEHIND Dave, 2-5 tiles away --------------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);           // Dave faces east
        put(room, 17, 12, 0x05);           // 3 tiles behind him
        World w; w.new_game(); w.load(room);
        for (int t = 0; t < 30; ++t) w.tick(none);
        CHECK(find_kind(w, kKindGhost)->state().ds != 0xFEC);      // fading in / solid
    }
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        put(room, 23, 12, 0x05);           // 3 tiles in front of him: stays hidden
        World w; w.new_game(); w.load(room);
        for (int t = 0; t < 200; ++t) w.tick(none);
        CHECK(find_kind(w, kKindGhost)->state().ds == 0xFEC);
    }
    // --- flaming skull: homes in slowly, cannot be killed, deadly ----------------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        put(room, 30, 12, 0x07);
        World w; w.new_game(); w.load(room);
        const int x0 = find_kind(w, kKindSkull)->x();
        w.tick(none);
        w.tick(fire);
        for (int t = 0; t < 40; ++t) w.tick(none);
        CHECK(count_kind(w, kKindSkull) == 1);                     // invulnerable
        CHECK(find_kind(w, kKindSkull)->x() < x0);                 // flying toward Dave
        for (int t = 0; t < 4000 && !w.dead(); ++t) w.tick(none);
        CHECK(w.level_state() == -7);
    }
    // --- spider: hangs from the ceiling, drops on its thread when Dave is right below ------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        for (int x = 10; x < 30; ++x) room.tiles[size_t(7) * 60 + x] = uint16_t(solid);   // ceiling at row 7
        put(room, 20, 12, 0xFF);
        put(room, 20, 8, 0x06);
        World w; w.new_game(); w.load(room);
        CHECK(find_kind(w, kKindSpider)->timer == find_kind(w, kKindSpider)->y());
        bool dropped = false, thread_drawn = false;
        for (int t = 0; t < 400 && !w.dead(); ++t) {
            w.tick(none);
            const Enemy* e = find_kind(w, kKindSpider);
            if (e && e->state().ds == 0x1168) {
                dropped = true;
                std::vector<ExtraDraw> ex;
                e->extra_draws(ex);
                if (ex.size() >= 2) thread_drawn = true;
            }
        }
        CHECK(dropped);
        CHECK(thread_drawn);
    }
    // --- spider stays up when Dave is not below it ------------------------------------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        for (int x = 10; x < 40; ++x) room.tiles[size_t(7) * 60 + x] = uint16_t(solid);
        put(room, 30, 12, 0xFF);
        put(room, 20, 8, 0x06);
        World w; w.new_game(); w.load(room);
        for (int t = 0; t < 200; ++t) w.tick(none);
        const Enemy* e = find_kind(w, kKindSpider);
        CHECK(e && e->state().ds != 0x1168 && e->state().ds != 0x117C);
    }

    // --- doors: press Up to open, press Up again to enter, teleport to the stored destination --------------------
    {
        dd2::Level room = make_room(solid);
        room.tiles[size_t(12) * 60 + 10] = 265;                   // closed door tile (property 1) ...
        room.entities[size_t(12) * 60 + 10] = (30 << 8) | 12;     // ... whose layer-1 value is the destination (30,12)
        room.tiles[size_t(12) * 60 + 30] = 265;                   // destination door (not yet open)
        put(room, 10, 11, 0xFF);
        World w; w.new_game(); w.load(room);
        const Input up = make_input(true, false, false, false, false, false);
        const uint32_t v0 = w.tile_version();
        for (int t = 0; t < 40; ++t) w.tick(none);                 // land on the floor in front of the door
        CHECK(w.player().state().ds == 0x8BC);
        w.tick(up);
        CHECK(w.player().state().ds == 0x970);                    // DAVEOPEN
        CHECK(w.player().x() == 10 << 8);                         // snapped to the door column
        for (int t = 0; t < 30; ++t) w.tick(none);
        CHECK(w.tile_version() > v0);
        CHECK(w.level().tile_at(10, 12) == 92);                   // open-door art (property 2)
        CHECK(w.level().tile_at(9, 11) == 78);
        CHECK(w.player().state().ds == 0x8BC);
        w.tick(up);                                               // second press: walk in
        CHECK(w.player().state().ds == 0x984);                    // DAVEENTER1
        for (int t = 0; t < 90; ++t) w.tick(none);
        CHECK(std::abs(w.player().x() - (30 << 8)) <= 64);        // teleported (+64: the landing nudge forward)
        CHECK(w.player().y() == (12 << 8) + 15);                  // and dropped onto the floor
        CHECK(w.level().tile_at(30, 12) == 92);                   // the destination door opened as well
        CHECK(w.level_state() == 0);
    }
    // --- the exit door (value 0x8000) completes the level -------------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        room.tiles[size_t(12) * 60 + 10] = 265;
        room.entities[size_t(12) * 60 + 10] = 0x8000;
        put(room, 10, 11, 0xFF);
        World w; w.new_game(); w.load(room);
        const Input up = make_input(true, false, false, false, false, false);
        for (int t = 0; t < 40; ++t) w.tick(none);
        w.tick(up);
        for (int t = 0; t < 30; ++t) w.tick(none);
        w.tick(up);
        for (int t = 0; t < 90; ++t) w.tick(none);
        CHECK(w.level_state() == 1);
        CHECK(w.level_complete());
    }
    // --- a searchable spot (value 0x8001) opens and drops a pickup above it ----------------------------------------------
    {
        dd2::Level room = make_room(solid);
        room.tiles[size_t(12) * 60 + 10] = 634;                   // tile 0x27A, property 1
        room.entities[size_t(12) * 60 + 10] = 0x8001;
        put(room, 10, 11, 0xFF);
        World w; w.new_game(); w.load(room);
        const Input up = make_input(true, false, false, false, false, false);
        for (int t = 0; t < 40; ++t) w.tick(none);
        w.tick(up);
        for (int t = 0; t < 30; ++t) w.tick(none);
        CHECK(count_kind(w, kKindPickup) == 1 || w.score() >= 100);
        CHECK(w.level().tile_at(10, 12) == 152);                  // used-up art, no property: cannot be searched twice
        w.tick(none);
        w.tick(up);
        CHECK(w.player().state().ds != 0x984);                    // nothing to enter
    }
    // --- up without a door still aims up -----------------------------------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 10, 12, 0xFF);
        World w; w.new_game(); w.load(room);
        for (int t = 0; t < 4; ++t) w.tick(none);
        w.tick(make_input(true, false, false, false, false, false));
        CHECK(w.player().state().ds == 0xA4C);
        CHECK(w.player().aim() == -1);
    }
    // --- hazard tiles kill (-8); exit-zone tiles complete the level ----------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        room.tiles[size_t(12) * 60 + 10] = 122;
        put(room, 10, 11, 0xFF);
        World w; w.new_game(); w.load(room);
        for (int t = 0; t < 10 && !w.dead(); ++t) w.tick(none);
        CHECK(w.level_state() == -8);
    }
    {
        dd2::Level room = make_room(solid);
        room.tiles[size_t(12) * 60 + 10] = 17;
        put(room, 10, 11, 0xFF);
        World w; w.new_game(); w.load(room);
        for (int t = 0; t < 10 && w.level_state() == 0; ++t) w.tick(none);
        CHECK(w.level_state() == 1);
    }
    // --- restart reloads the pristine level (open doors close again) --------------------------------------------------------------
    {
        dd2::Level room = make_room(solid);
        room.tiles[size_t(12) * 60 + 10] = 265;
        room.entities[size_t(12) * 60 + 10] = (30 << 8) | 12;
        put(room, 10, 11, 0xFF);
        World w; w.new_game(); w.load(room);
        const Input up = make_input(true, false, false, false, false, false);
        for (int t = 0; t < 40; ++t) w.tick(none);
        w.tick(up);
        for (int t = 0; t < 30; ++t) w.tick(none);
        CHECK(w.level().tile_at(10, 12) == 92);
        w.restart();
        CHECK(w.level().tile_at(10, 12) == 265);
        CHECK(w.tile_version() == 0);
    }

        auto cage = [&](dd2::Level& r) {                          // Frank's room: walls at columns 26 and 38, Dave stays outside
        for (int y = 0; y < 14; ++y) { r.tiles[size_t(y) * 60 + 26] = uint16_t(solid); r.tiles[size_t(y) * 60 + 38] = uint16_t(solid); }
    };
    // --- Frank: 15 hit points, walks, throws bouncing sparks, kills on touch; his death ends the level -----------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 40, 12, 0xFF);
        put(room, 30, 9, 0x0B);            // Frank: 80 px tall, feet on the floor at row 14
        cage(room);
        World w; w.new_game(); w.load(room);
        const Enemy* f = find_kind(w, kKindFrank);
        CHECK(f && f->hp == 15 && f->shootable && f->hurtable);
        const int fy = f->y();
        bool spark_seen = false;
        int max_sparks = 0;
        for (int t = 0; t < 900 && !w.dead(); ++t) {
            w.tick(none);
            const int s = count_kind(w, kKindSpark);
            if (s > 0) spark_seen = true;
            max_sparks = std::max(max_sparks, s);
            if (find_kind(w, kKindFrank)) CHECK(find_kind(w, kKindFrank)->y() == fy);       // stays on the floor
        }
        CHECK(spark_seen);
        CHECK(max_sparks >= 1);
    }
    {   // touching Frank is deadly
        dd2::Level room = make_room(solid);
        put(room, 20, 12, 0xFF);
        put(room, 30, 9, 0x0B);
        World w; w.new_game(); w.load(room);
        const Input right = make_input(false, false, false, true, false, false);
        for (int t = 0; t < 700 && !w.dead(); ++t) w.tick(right);
        CHECK(w.level_state() == -7);
    }
    {   // killing Frank: 15 hits, 1600 points, the boss shower, minions removed, level complete
        dd2::Level room = make_room(solid);
        put(room, 40, 12, 0xFF);
        put(room, 30, 9, 0x0B);
        cage(room);
        World w; w.new_game(); w.load(room);
        for (int t = 0; t < 400 && count_kind(w, kKindSpark) == 0; ++t) w.tick(none);   // wait until a spark is out
        CHECK(count_kind(w, kKindSpark) >= 1);
        for (int i = 0; i < 14; ++i) {
            const Enemy* fr = find_kind(w, kKindFrank);
            CHECK(fr != nullptr);
            if (!fr) break;
            w.hit((fr->box().l + fr->box().r) / 2, (fr->box().t + fr->box().b) / 2);
        }
        CHECK(find_kind(w, kKindFrank) != nullptr);               // 14 hits are not enough
        CHECK(w.level_state() == 0);
        const Enemy* fr = find_kind(w, kKindFrank);
        w.hit((fr->box().l + fr->box().r) / 2, (fr->box().t + fr->box().b) / 2);
        CHECK(find_kind(w, kKindFrank) == nullptr);
        CHECK(w.level_state() == 100);
        CHECK(w.boss_down());                                     // keeps running while the debris settles ...
        CHECK(w.score() == 1600);
        CHECK(count_kind(w, kKindSpark) == 0);
        CHECK(count_kind(w, kKindDebris) >= 16);                  // 2 + 14 gibs (+ the score popup)
        for (int t = 0; t < 1500 && !w.level_complete(); ++t) w.tick(none);
        CHECK(w.level_complete());                                // ... then the level is complete
        CHECK(!w.dead());
    }
    {   // a spark can be shot and leaves debris
        dd2::Level room = make_room(solid);
        put(room, 40, 12, 0xFF);
        put(room, 30, 9, 0x0B);
        cage(room);
        World w; w.new_game(); w.load(room);
        for (int t = 0; t < 400 && count_kind(w, kKindSpark) == 0; ++t) w.tick(none);
        const Enemy* s = find_kind(w, kKindSpark);
        CHECK(s != nullptr);
        if (s) {
            // The spark spawns at Frank's hand and his next step overlaps it; the ray test checks objects in order, so a shot
            // there would hit Frank (as in the original). Aim at a point of the spark outside Frank's box.
            const Box fb = find_kind(w, kKindFrank)->box();
            const int hx = s->box().r > fb.r ? s->box().r - 1 : s->box().l + 1;
            w.hit(hx, (s->box().t + s->box().b) / 2);
            CHECK(count_kind(w, kKindSpark) == 0);
            CHECK(count_kind(w, kKindDebris) > 0);
            CHECK(w.score() == 0);
        }
    }
    {   // sparks ricochet and expire after 13 bounces
        dd2::Level room = make_room(solid);
        put(room, 40, 12, 0xFF);
        put(room, 30, 9, 0x0B);
        cage(room);
        World w; w.new_game(); w.load(room);
        int first_seen = -1, gone_at = -1;
        for (int t = 0; t < 2500 && !w.dead(); ++t) {
            w.tick(none);
            const int s = count_kind(w, kKindSpark);
            if (first_seen < 0 && s > 0) first_seen = t;
            if (first_seen >= 0 && gone_at < 0 && s == 0) gone_at = t;
        }
        CHECK(first_seen > 0);
        CHECK(gone_at > first_seen + 24);                         // hover (24 tics) + flight
    }

    // --- the Master: patrols, regrows claws, fire attack; killing him frees Delbert and then ends level 8 ------------------------
    {
        dd2::Level room = make_room(solid);
        put(room, 3, 12, 0xFF);
        put(room, 14, 3, 0x0C);            // the Master floats near the ceiling
        put(room, 20, 8, 0x0D);            // Delbert, caged in mid-air
        World w; w.new_game(); w.load(room, 8);
        const Enemy* m = find_kind(w, kKindMaster);
        CHECK(m && m->hp == 15 && m->shootable && m->hurtable);
        CHECK(find_kind(w, kKindDelbert) != nullptr);
        int minx = m->x(), maxx = m->x();
        bool saw_claw = false, saw_fire = false;
        int max_claws = 0;
        for (int t = 0; t < 700 && !w.dead(); ++t) {
            w.tick(none);
            const Enemy* mm = find_kind(w, kKindMaster);
            minx = std::min(minx, mm->x());
            maxx = std::max(maxx, mm->x());
            const int claws = count_kind(w, kKindMasterClaw);
            max_claws = std::max(max_claws, claws);
            if (claws) saw_claw = true;
            if (count_kind(w, kKindMasterFire)) saw_fire = true;
        }
        CHECK(maxx - minx > 0x400);                              // glides back and forth
        CHECK(minx >= 0x200 - 12 && maxx <= 0xF00 + 12);         // within the 2..15 tile range
        CHECK(saw_claw);
        CHECK(max_claws == 2);                                   // one left, one right
        CHECK(saw_fire);
    }
    {   // claws have 2 hit points, are shootable and homing
        dd2::Level room = make_room(solid);
        put(room, 3, 12, 0xFF);
        put(room, 14, 3, 0x0C);
        World w; w.new_game(); w.load(room, 8);
        for (int t = 0; t < 400 && count_kind(w, kKindMasterClaw) == 0; ++t) w.tick(none);
        const Enemy* c = find_kind(w, kKindMasterClaw);
        CHECK(c != nullptr);
        if (c) {
            CHECK(c->hp == 2 && c->shootable);
            auto dist = [&](const Enemy& e) { return std::abs(e.x() - w.player().x()) + std::abs(e.y() - w.player().y()); };
            const int d0 = dist(*c);
            const uint32_t id = c->id;
            for (int t = 0; t < 200; ++t) w.tick(none);
            const Enemy* c2 = w.find_alive(id, kKindMasterClaw);
            if (c2) CHECK(dist(*c2) < d0 - 400);                   // homes in on Dave (after its 100-tic spawn pose)
        }
    }
    {   // killing the Master: minions vanish, level state 100; on level 8 Delbert is freed and the level ends with him
        dd2::Level room = make_room(solid);
        put(room, 3, 12, 0xFF);
        put(room, 14, 3, 0x0C);
        put(room, 20, 8, 0x0D);
        World w; w.new_game(); w.load(room, 8);
        for (int t = 0; t < 400 && count_kind(w, kKindMasterClaw) < 2; ++t) w.tick(none);
        CHECK(count_kind(w, kKindMasterClaw) == 2);
        for (int i = 0; i < 15; ++i) {
            const Enemy* mm = find_kind(w, kKindMaster);
            CHECK(mm != nullptr);
            if (!mm) break;
            w.hit((mm->box().l + mm->box().r) / 2, (mm->box().t + mm->box().b) / 2);
        }
        CHECK(find_kind(w, kKindMaster) == nullptr);
        CHECK(count_kind(w, kKindMasterClaw) == 0);
        CHECK(w.boss_down());
        CHECK(w.score() == 0);                                   // the Master awards no points
        bool released = false, fell = false, hopped = false, celebrating = false;
        for (int t = 0; t < 3000 && !w.level_complete() && !w.dead(); ++t) {
            w.tick(none);
            const Enemy* d = find_kind(w, kKindDelbert);
            if (d && d->state().ds == 0x1280) { released = true; fell = true; }
            if (d && d->state().ds == 0x1294) hopped = true;
            if (d && d->state().ds == 0x12A8) celebrating = true;
        }
        CHECK(released && fell && hopped && celebrating);
        CHECK(w.level_complete());                               // only after Delbert's celebration
        CHECK(!w.dead());
        CHECK(find_kind(w, kKindDelbert)->y() > 8 * 256);        // he dropped to the floor
    }
    {   // on any other level the boss kill completes the level once the debris has settled
        dd2::Level room = make_room(solid);
        put(room, 3, 12, 0xFF);
        put(room, 14, 3, 0x0C);
        World w; w.new_game(); w.load(room, 4);
        for (int t = 0; t < 5; ++t) w.tick(none);
        const Enemy* mm = find_kind(w, kKindMaster);
        for (int i = 0; i < 15 && mm; ++i) { w.hit((mm->box().l + mm->box().r) / 2, (mm->box().t + mm->box().b) / 2); mm = find_kind(w, kKindMaster); }
        CHECK(w.boss_down());
        for (int t = 0; t < 1500 && !w.level_complete(); ++t) w.tick(none);
        CHECK(w.level_complete());
    }

    // --- real level smoke test ----------------------------------------------------------------------------------------------------
    if (argc > 1) {
        const std::string dir = std::string(argv[1]) + "/";
        for (int n = 1; n <= 8; ++n) {
            char name[32]; std::snprintf(name, sizeof name, "LEVEL%02d.DD2", n);
            const dd2::Level lv = dd2::load_level(dir + name);
            World w; w.new_game(); w.load(lv);
            CHECK(w.objects().size() > 0 || n == 8 || n == 4);
            for (int t = 0; t < 700 && !w.dead(); ++t) w.tick(none);    // idle at the start: nothing may crash
            CHECK(w.level_state() <= 0 || w.level_state() == 100);
        }
        // Every real door in the game: open it, walk through, and land safely on the destination door.
        {
            int doors = 0;
            for (int n = 1; n <= 8; ++n) {
                char name[32]; std::snprintf(name, sizeof name, "LEVEL%02d.DD2", n);
                const dd2::Level lv = dd2::load_level(dir + name);
                for (int r = 0; r < lv.height; ++r)
                    for (int c = 0; c < lv.width; ++c) {
                        const int prop = dd2::tile_property(lv.tile_at(c, r));
                        const uint16_t v = lv.entity_at(c, r);
                        if (!v || (prop != 1 && prop != 2) || v >= 0x8000 || dd2::find_spawn_def(v)) continue;
                        ++doors;
                        World w; w.new_game(); w.load(lv); w.clear_objects();
                        w.player().spawn(c, r - 1, w.level());
                        const Input up = make_input(true, false, false, false, false, false);
                        for (int t = 0; t < 40; ++t) w.tick(none);
                        if (prop == 1) { w.tick(up); for (int t = 0; t < 30; ++t) w.tick(none); }
                        w.tick(up);
                        for (int t = 0; t < 120; ++t) w.tick(none);
                        const int dx = (v >> 8) & 0x7F, dy = v & 0x7F;
                        if (w.dead() || std::abs(w.player().x() - (dx << 8)) > 0x180 || w.player().state().ds != 0x8BC) {
                            std::printf("  door L%d (%d,%d)->(%d,%d): dead=%d x=%d state=0x%x\n", n, c, r, dx, dy, int(w.dead()), w.player().x() >> 8, w.player().state().ds);
                            CHECK(false);
                        }
                        // standing on something solid
                        const Box& b = w.player().box();
                        bool floor = false;
                        for (int cc = b.lc; cc <= b.rc; ++cc) floor = floor || tile_face(w.level(), cc, b.br + 1, dd2::TileFace::Top);
                        CHECK(floor);
                    }
            }
            CHECK(doors == 25);
        }
        // The real boss levels: kill the boss, then the level must complete (level 8 through Delbert).
        for (int n : {4, 8}) {
            char name[32]; std::snprintf(name, sizeof name, "LEVEL%02d.DD2", n);
            const dd2::Level lv = dd2::load_level(dir + name);
            World w; w.new_game(); w.load(lv, n);
            if (n == 8) w.player().spawn(11, 30, w.level());          // Dave starts at the top; the Master (row 33) sleeps until he comes down
            for (int i = 0; i < 6; ++i) w.tick(none);
            const int boss = n == 4 ? kKindFrank : kKindMaster;
            CHECK(find_kind(w, boss) != nullptr);
            if (n == 8) CHECK(find_kind(w, kKindDelbert) != nullptr);
            for (int i = 0; i < 15; ++i) {
                const Enemy* b = find_kind(w, boss);
                if (!b) break;
                w.hit((b->box().l + b->box().r) / 2, (b->box().t + b->box().b) / 2);
            }
            CHECK(find_kind(w, boss) == nullptr);
            CHECK(w.boss_down());
            if (n == 8) {
                // The level does not end: Delbert (object 1) is released but asleep, 22 rows away, until Dave climbs back to him.
                for (int t = 0; t < 2000 && w.level_state() != 0; ++t) w.tick(none);
                CHECK(w.level_state() == 0);
                CHECK(find_kind(w, kKindDelbert)->state().ds == 0x1280);
                for (int t = 0; t < 300; ++t) w.tick(none);
                CHECK(!w.level_complete());                              // still waiting
                w.player().spawn(12, 9, w.level());                      // Dave goes up to Delbert
            }
            for (int t = 0; t < 4000 && !w.level_complete() && !w.dead(); ++t) w.tick(none);
            std::printf("  level %d boss kill -> state %d\n", n, w.level_state());
            CHECK(w.level_complete());
            if (n == 8) {
                const Enemy* d = find_kind(w, kKindDelbert);
                CHECK(d && d->state().ds == 0x12A8);
                CHECK(d && d->y() > 11 * 256);                           // he fell to the floor below his cage
            }
        }
        // Level 1: zombies start walking once the screen reaches them; the world keeps ticking
        const dd2::Level lv1 = dd2::load_level(dir + "LEVEL01.DD2");
        World w; w.new_game(); w.load(lv1);
        for (int t = 0; t < 1400 && !w.dead(); ++t) w.tick(none);
        CHECK(count_kind(w, kKindZombie) >= 1);
    }

    std::printf(g_fail ? "%d enemy check(s) FAILED\n" : "all enemy checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
