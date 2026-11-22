// SPDX-License-Identifier: MIT
// ddhm_viewer - SDL3 level browser for Dangerous Dave in the Haunted Mansion.
// Stage-1 checkpoint of the port: proves asset decoding + SDL3 rendering on Windows 11.
//
// Controls:  1-8 = level    Arrow keys = scroll (hold Shift = fast)
//            P = play mode: arrows move Dave (Up = aim up, Down = aim down), Ctrl / Space / Z = jump (original: Ctrl),
//                Alt / X = shoot (original: Alt), Down + jump = drop through a one-way floor, R = restart after dying,
//                Up at a door = open it, Up again = go through; Enter = next level when the level is complete
//            C = collision faces / tile properties overlay
//            S = toggle entity sprites (Dave, enemies, pickups)    E = raw entity-code boxes
//            F11 = fullscreen    Esc = quit
#include "dd2/exe.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <string>

#include "dd2/dd2.hpp"
#include "game/world.hpp"

namespace {
constexpr int kLogicalW = 320, kLogicalH = 200;   // original EGA resolution

struct LevelView {
    dd2::Level level;
    SDL_Texture* tex = nullptr;
    SDL_Texture* texSprites = nullptr;
    SDL_Texture* texCollision = nullptr;
    dd2::EntityList entities;
    int pxW = 0, pxH = 0;
};

SDL_Texture* make_texture(SDL_Renderer* r, const std::vector<uint8_t>& rgba, int w, int h) {
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) { SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError()); return nullptr; }
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    SDL_UpdateTexture(t, nullptr, rgba.data(), w * 4);
    return t;
}

bool load_level_view(SDL_Renderer* r, const dd2::TileSet& ts, const std::vector<dd2::Sprite>& sprites,
                     const std::string& dir, int n, LevelView& out) {
    char name[32];
    std::snprintf(name, sizeof name, "LEVEL%02d.DD2", n);
    try {
        out.level = dd2::load_level(dir + name);
    } catch (const std::exception& e) {
        SDL_Log("Cannot load %s: %s", name, e.what());
        return false;
    }
    out.pxW = out.level.width * dd2::kTileSize;
    out.pxH = out.level.height * dd2::kTileSize;
    out.entities = dd2::extract_entities(out.level);

    const auto tilesOnly = dd2::render_level_rgba(out.level, ts);
    auto withSprites = tilesOnly;
    for (const auto& s : out.entities.spawns)
        if (s.sprite > 0 && size_t(s.sprite) < sprites.size())
            dd2::blit_sprite_rgba(withSprites, out.pxW, out.pxH, sprites[size_t(s.sprite)], s.px, s.py);

    if (out.tex) SDL_DestroyTexture(out.tex);
    if (out.texSprites) SDL_DestroyTexture(out.texSprites);
    out.tex = make_texture(r, tilesOnly, out.pxW, out.pxH);
    out.texSprites = sprites.empty() ? nullptr : make_texture(r, withSprites, out.pxW, out.pxH);
    if (out.texCollision) SDL_DestroyTexture(out.texCollision);
    out.texCollision = make_texture(r, dd2::render_collision_overlay_rgba(out.level), out.pxW, out.pxH);
    if (out.texCollision) SDL_SetTextureBlendMode(out.texCollision, SDL_BLENDMODE_BLEND);
    return out.tex != nullptr;
}
}  // namespace

int main(int argc, char** argv) {
    // The game tables (sprites, states, collision, RNG) come from the user's DAVE.EXE, never from this project.
    if (argc < 2) { std::printf("usage: %s <game-dir or Assets-dir> ...\n", argv[0]); return 2; }
    try { dd2::load_exe_from_dir(argv[1]); } catch (const std::exception& e) { std::printf("cannot load DAVE.EXE: %s\n", e.what()); return 1; }
    const std::string dir = (argc > 1 ? std::string(argv[1]) : std::string(".")) + "/";

    dd2::TileSet tiles;
    try {
        tiles = dd2::load_tileset(dir + "EGATILES.DD2");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Could not load game data from '%s': %s\nusage: ddhm_viewer <folder with the .DD2 files>\n",
                     dir.c_str(), e.what());
        return 1;
    }

    std::vector<dd2::Sprite> sprites;
    try {
        sprites = dd2::load_sprites(dir);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Sprites unavailable (%s); showing tiles only.\n", e.what());
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return 1; }
    SDL_Window* window = SDL_CreateWindow("Dangerous Dave in the Haunted Mansion - level viewer",
                                          1280, 800, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) { SDL_Log("Window/renderer failed: %s", SDL_GetError()); return 1; }
    SDL_SetRenderLogicalPresentation(renderer, kLogicalW, kLogicalH, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    std::vector<SDL_Texture*> spriteTex;
    for (const auto& s : sprites) {
        SDL_Texture* t = make_texture(renderer, dd2::sprite_to_rgba(s), s.width, s.height);
        if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        spriteTex.push_back(t);
    }
    ddhm::World world;
    bool play = false;
    double tickAcc = 0;
    uint32_t shownVersion = ~0u;               // tile_version of the level picture currently in view.tex

    LevelView view;
    int levelNo = 1;
    float camX = 0, camY = 0;
    bool showEntities = false, showSprites = true, showCollision = false, running = true;
    if (!load_level_view(renderer, tiles, sprites, dir, levelNo, view)) return 1;

    auto start_play = [&]() {
        if (view.entities.player_index < 0) return;
        world.new_game();
        world.load(view.level, levelNo);
        tickAcc = 0;
        shownVersion = ~0u;
        play = true;
    };

    Uint64 last = SDL_GetTicks();
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) running = false;
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                const SDL_Keycode k = ev.key.key;
                if (k == SDLK_ESCAPE) running = false;
                else if (k == SDLK_E) showEntities = !showEntities;
                else if (k == SDLK_S) showSprites = !showSprites;
                else if (k == SDLK_C) showCollision = !showCollision;
                else if (k == SDLK_P) { if (play) play = false; else start_play(); }
                else if (k == SDLK_R && play && world.dead()) {
                    if (world.lives() > 1) world.restart();
                    else { world.new_game(); world.load(view.level, levelNo); }      // out of lives: new game
                    tickAcc = 0;
                    shownVersion = ~0u;
                } else if (k == SDLK_RETURN && play && world.level_complete()) {
                    if (levelNo < 8) {                                         // on to the next level
                        if (load_level_view(renderer, tiles, sprites, dir, levelNo + 1, view)) { ++levelNo; camX = camY = 0; }
                    } else {                                                   // finished the game: start over
                        if (load_level_view(renderer, tiles, sprites, dir, 1, view)) { levelNo = 1; camX = camY = 0; }
                        world.new_game();
                    }
                    world.load(view.level, levelNo);
                    tickAcc = 0;
                    shownVersion = ~0u;
                }
                else if (k == SDLK_F11) SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
                else if (k >= SDLK_1 && k <= SDLK_8) {
                    const int n = int(k - SDLK_1) + 1;
                    if (load_level_view(renderer, tiles, sprites, dir, n, view)) { levelNo = n; camX = camY = 0; if (play) { world.load(view.level, levelNo); tickAcc = 0; shownVersion = ~0u; } }
                }
            }
        }
        const Uint64 now = SDL_GetTicks();
        const float dt = float(now - last) / 1000.0f;
        last = now;

        const bool* ks = SDL_GetKeyboardState(nullptr);
        if (play) {
            const ddhm::Input in = ddhm::make_input(
                ks[SDL_SCANCODE_UP], ks[SDL_SCANCODE_DOWN], ks[SDL_SCANCODE_LEFT], ks[SDL_SCANCODE_RIGHT],
                ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL] || ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_Z],
                ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT] || ks[SDL_SCANCODE_X]);
            tickAcc = std::min(tickAcc + double(dt), 0.25);
            const double step = 1.0 / ddhm::kTicsPerSecond;
            while (tickAcc >= step) { world.tick(in); tickAcc -= step; }
            if (world.tile_version() != shownVersion) {                    // a door opened (or a level was reloaded)
                SDL_UpdateTexture(view.tex, nullptr, dd2::render_level_rgba(world.level(), tiles).data(), view.pxW * 4);
                shownVersion = world.tile_version();
            }
            camX = float(world.player().camera().x) / 16.0f;
            camY = float(world.player().camera().y) / 16.0f;
        } else {
            const float speed = (ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT] ? 600.0f : 200.0f) * dt;
            if (ks[SDL_SCANCODE_LEFT])  camX -= speed;
            if (ks[SDL_SCANCODE_RIGHT]) camX += speed;
            if (ks[SDL_SCANCODE_UP])    camY -= speed;
            if (ks[SDL_SCANCODE_DOWN])  camY += speed;
        }
        camX = std::clamp(camX, 0.0f, float(std::max(0, view.pxW - kLogicalW)));
        camY = std::clamp(camY, 0.0f, float(std::max(0, view.pxH - kLogicalH)));

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        const SDL_FRect src{camX, camY, float(std::min(kLogicalW, view.pxW)), float(std::min(kLogicalH, view.pxH))};
        const SDL_FRect dst{0, 0, src.w, src.h};
        SDL_Texture* shown = play ? view.tex : ((showSprites && view.texSprites) ? view.texSprites : view.tex);
        SDL_RenderTexture(renderer, shown, &src, &dst);
        if (play && showSprites) {
            auto draw_sprite = [&](int spr, int px, int py) {
                if (spr <= 0 || size_t(spr) >= spriteTex.size() || !spriteTex[size_t(spr)]) return;
                const SDL_FRect d{float(px) - camX, float(py) - camY, float(sprites[size_t(spr)].width),
                                  float(sprites[size_t(spr)].height)};
                SDL_RenderTexture(renderer, spriteTex[size_t(spr)], nullptr, &d);
            };
            std::vector<ddhm::ExtraDraw> extras;
            for (const auto& e : world.objects()) {
                if (!e.kind) continue;
                if (e.flash > 0 && (e.flash & 2)) continue;                // blink while hit
                draw_sprite(e.sprite(), e.draw_x(), e.draw_y());
                extras.clear();
                e.extra_draws(extras);                                    // spider thread and body
                for (const auto& x : extras)
                    draw_sprite(x.sprite, (x.x + dd2::kSpriteTable[x.sprite].draw_dx) >> 4, (x.y + dd2::kSpriteTable[x.sprite].draw_dy) >> 4);
            }
            for (const auto& e : world.player().effects()) {
                const int spr = e.facing > 0 ? e.state->sprite_e : e.state->sprite_w;
                draw_sprite(spr, (e.x + dd2::kSpriteTable[spr].draw_dx) >> 4, (e.y + dd2::kSpriteTable[spr].draw_dy) >> 4);
            }
            if (!world.dead()) draw_sprite(world.player().sprite(), world.player().draw_x(), world.player().draw_y());
        }
        if (showCollision && view.texCollision) SDL_RenderTexture(renderer, view.texCollision, &src, &dst);

        if (showEntities) {
            const int tx0 = int(camX) / 16, ty0 = int(camY) / 16;
            for (int ty = ty0; ty <= ty0 + kLogicalH / 16 + 1 && ty < view.level.height; ++ty)
                for (int tx = tx0; tx <= tx0 + kLogicalW / 16 + 1 && tx < view.level.width; ++tx) {
                    const uint16_t e = view.level.entity_at(tx, ty);
                    if (!e) continue;
                    const SDL_FRect box{float(tx * 16) - camX + 2, float(ty * 16) - camY + 2, 12, 12};
                    SDL_SetRenderDrawColor(renderer, 255, 0, 255, 150);
                    SDL_RenderFillRect(renderer, &box);
                }
        }
        SDL_RenderPresent(renderer);

        char title[200];
        if (play)
            std::snprintf(title, sizeof title, "DDHM - PLAY  level %d   score %u   lives %d   ammo %d   %s", levelNo,
                          world.score(), world.lives(),
                          world.player().ammo(),
                          world.dead() ? "** DEAD - press R **"
                                       : (world.level_complete() ? (levelNo < 8 ? "** LEVEL COMPLETE - press Enter **" : "** YOU WIN - press Enter **")
                                                                 : (world.boss_down() ? "** BOSS DOWN **" : "[P = viewer]")));
        else
            std::snprintf(title, sizeof title, "DDHM viewer - level %d (%dx%d tiles)  cam %d,%d%s%s   [P = play]", levelNo,
                          view.level.width, view.level.height, int(camX) / 16, int(camY) / 16,
                          (showSprites && view.texSprites) ? "  [sprites]" : "", showEntities ? "  [codes]" : "");
        SDL_SetWindowTitle(window, title);
        SDL_Delay(1);
    }
    for (SDL_Texture* t : spriteTex) if (t) SDL_DestroyTexture(t);
    if (view.tex) SDL_DestroyTexture(view.tex);
    if (view.texSprites) SDL_DestroyTexture(view.texSprites);
    if (view.texCollision) SDL_DestroyTexture(view.texCollision);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
