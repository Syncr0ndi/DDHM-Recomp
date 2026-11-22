// SPDX-License-Identifier: MIT
// ddhm - Dangerous Dave in the Haunted Mansion, native Windows port (SDL3 front end).
//
// Usage: ddhm [data-folder] [--window | --fullscreen] [--square-pixels]
// Normally started by DDHM Launcher. Next to ddhm.exe: "Game" holds the player's own copy of the MS-DOS game,
// "Assets" the files extracted from it on first launch. Without them the port does not start. A data folder on the
// command line (a Game or an Assets folder) is used directly, for development.
//
// Keys are the original's (F1 lists them): arrows move and aim, CTRL jumps, ALT fires, DOWN+CTRL drops through a
// floor, UP at a door opens / enters it, F2 sound, F3 key setup, F4 gamepad setup, F5 reset, TAB status, ESC quit.
// Port extras: Space or Z also jump, X also fires (unless F3 bound them to something else), F11 or Alt+Enter toggles
// fullscreen, F9 toggles 4:3 aspect correction, F8 toggles a softer (filtered) speaker sound. A gamepad works too (D-pad / left stick, A jump, B or X fire).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "dd2/assets.hpp"
#include "game/flow.hpp"
#include "game/machine.hpp"
#include "game/speaker.hpp"

namespace fs = std::filesystem;

namespace {

// SDL scancode -> PC keyboard scan code (set 1), for every key the game can see.
int pc_scancode(SDL_Scancode s) {
    switch (s) {
        case SDL_SCANCODE_ESCAPE: return 0x01;
        case SDL_SCANCODE_1: return 0x02; case SDL_SCANCODE_2: return 0x03; case SDL_SCANCODE_3: return 0x04;
        case SDL_SCANCODE_4: return 0x05; case SDL_SCANCODE_5: return 0x06; case SDL_SCANCODE_6: return 0x07;
        case SDL_SCANCODE_7: return 0x08; case SDL_SCANCODE_8: return 0x09; case SDL_SCANCODE_9: return 0x0A;
        case SDL_SCANCODE_0: return 0x0B; case SDL_SCANCODE_MINUS: return 0x0C; case SDL_SCANCODE_EQUALS: return 0x0D;
        case SDL_SCANCODE_BACKSPACE: return 0x0E; case SDL_SCANCODE_TAB: return 0x0F;
        case SDL_SCANCODE_Q: return 0x10; case SDL_SCANCODE_W: return 0x11; case SDL_SCANCODE_E: return 0x12;
        case SDL_SCANCODE_R: return 0x13; case SDL_SCANCODE_T: return 0x14; case SDL_SCANCODE_Y: return 0x15;
        case SDL_SCANCODE_U: return 0x16; case SDL_SCANCODE_I: return 0x17; case SDL_SCANCODE_O: return 0x18;
        case SDL_SCANCODE_P: return 0x19; case SDL_SCANCODE_LEFTBRACKET: return 0x1A; case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x1C;
        case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x1D;
        case SDL_SCANCODE_A: return 0x1E; case SDL_SCANCODE_S: return 0x1F; case SDL_SCANCODE_D: return 0x20;
        case SDL_SCANCODE_F: return 0x21; case SDL_SCANCODE_G: return 0x22; case SDL_SCANCODE_H: return 0x23;
        case SDL_SCANCODE_J: return 0x24; case SDL_SCANCODE_K: return 0x25; case SDL_SCANCODE_L: return 0x26;
        case SDL_SCANCODE_SEMICOLON: return 0x27; case SDL_SCANCODE_APOSTROPHE: return 0x28; case SDL_SCANCODE_GRAVE: return 0x29;
        case SDL_SCANCODE_LSHIFT: return 0x2A; case SDL_SCANCODE_BACKSLASH: return 0x2B;
        case SDL_SCANCODE_Z: return 0x2C; case SDL_SCANCODE_X: return 0x2D; case SDL_SCANCODE_C: return 0x2E;
        case SDL_SCANCODE_V: return 0x2F; case SDL_SCANCODE_B: return 0x30; case SDL_SCANCODE_N: return 0x31;
        case SDL_SCANCODE_M: return 0x32; case SDL_SCANCODE_COMMA: return 0x33; case SDL_SCANCODE_PERIOD: return 0x34;
        case SDL_SCANCODE_SLASH: return 0x35; case SDL_SCANCODE_RSHIFT: return 0x36; case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
        case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x38; case SDL_SCANCODE_SPACE: return 0x39;
        case SDL_SCANCODE_CAPSLOCK: return 0x3A;
        case SDL_SCANCODE_F1: return 0x3B; case SDL_SCANCODE_F2: return 0x3C; case SDL_SCANCODE_F3: return 0x3D;
        case SDL_SCANCODE_F4: return 0x3E; case SDL_SCANCODE_F5: return 0x3F; case SDL_SCANCODE_F6: return 0x40;
        case SDL_SCANCODE_F7: return 0x41; case SDL_SCANCODE_F8: return 0x42; case SDL_SCANCODE_F9: return 0x43;
        case SDL_SCANCODE_F10: return 0x44; case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45; case SDL_SCANCODE_SCROLLLOCK: return 0x46;
        case SDL_SCANCODE_HOME: case SDL_SCANCODE_KP_7: return 0x47;
        case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8: return 0x48;
        case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_KP_9: return 0x49;
        case SDL_SCANCODE_KP_MINUS: return 0x4A;
        case SDL_SCANCODE_LEFT: case SDL_SCANCODE_KP_4: return 0x4B;
        case SDL_SCANCODE_KP_5: return 0x4C;
        case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_KP_6: return 0x4D;
        case SDL_SCANCODE_KP_PLUS: return 0x4E;
        case SDL_SCANCODE_END: case SDL_SCANCODE_KP_1: return 0x4F;
        case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2: return 0x50;
        case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_KP_3: return 0x51;
        case SDL_SCANCODE_INSERT: case SDL_SCANCODE_KP_0: return 0x52;
        case SDL_SCANCODE_DELETE: case SDL_SCANCODE_KP_PERIOD: return 0x53;
        case SDL_SCANCODE_F11: return 0x57; case SDL_SCANCODE_F12: return 0x58;
        default: return 0;
    }
}

struct Settings {                                   // ddhm.ini in the user's preference folder (shared with the launcher)
    bool fullscreen = false, square_pixels = false, soft_sound = false;
    void load(const fs::path& p) {
        std::ifstream f(p);
        std::string line;
        while (std::getline(f, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "fullscreen") fullscreen = v == "1";
            else if (k == "square_pixels") square_pixels = v == "1";
            else if (k == "soft_sound") soft_sound = v == "1";
        }
    }
    void save(const fs::path& p) const {
        std::ofstream f(p, std::ios::trunc);
        f << "fullscreen=" << (fullscreen ? 1 : 0) << "\nsquare_pixels=" << (square_pixels ? 1 : 0)
          << "\nsoft_sound=" << (soft_sound ? 1 : 0) << "\n";
    }
};

void fail(const std::string& msg) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dangerous Dave in the Haunted Mansion", msg.c_str(), nullptr);
}

// The window icon is drawn from the game's own sprite (DAVESTANDE) at run time, so no game art is stored in the port.
void set_icon(SDL_Window* w, const ddhm::Assets& a) {
    if (a.sprites.size() < 2) return;
    const dd2::Sprite& s = a.sprites[1];
    const int size = 32;
    SDL_Surface* surf = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return;
    SDL_FillSurfaceRect(surf, nullptr, 0);
    auto* px = static_cast<uint8_t*>(surf->pixels);
    const int ox = (size - s.width) / 2, oy = (size - s.height) / 2;
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x) {
            const uint8_t c = s.pixels[size_t(y * s.width + x)];
            if (c == dd2::Sprite::kTransparent || ox + x < 0 || oy + y < 0 || ox + x >= size || oy + y >= size) continue;
            const dd2::Rgb rgb = dd2::kEgaPalette[c & 15];
            uint8_t* p = px + (oy + y) * surf->pitch + (ox + x) * 4;
            p[0] = rgb.r; p[1] = rgb.g; p[2] = rgb.b; p[3] = 255;
        }
    SDL_SetWindowIcon(w, surf);
    SDL_DestroySurface(surf);
}

}  // namespace

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    fs::path pref;
    if (char* p = SDL_GetPrefPath("DDHM-Recomp", "DDHM")) { pref = dd2::utf8_path(p); SDL_free(p); }
    Settings settings;
    if (!pref.empty()) settings.load(pref / "ddhm.ini");

    std::string arg_dir;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--window") settings.fullscreen = false;
        else if (a == "--fullscreen") settings.fullscreen = true;
        else if (a == "--square-pixels") settings.square_pixels = true;
        else arg_dir = a;
    }

    // Data: the player's own copy of the MS-DOS game goes into "Game" next to ddhm.exe; the files the port needs are
    // extracted from it into "Assets" (normally by DDHM Launcher, otherwise here on first start). The port ships no
    // game data and does not start without it. A folder given on the command line (Game or Assets) is used directly.
    fs::path base = fs::current_path();
    if (const char* b = SDL_GetBasePath()) base = dd2::utf8_path(b);
    const fs::path game_dir = base / "Game";
    fs::path data_dir = base / "Assets";
    if (!arg_dir.empty()) {
        data_dir = dd2::utf8_path(arg_dir);
    } else if (!dd2::check_assets(dd2::path_utf8(data_dir)).ok) {
        const dd2::FolderCheck g = dd2::check_game_folder(dd2::path_utf8(game_dir));
        if (!g.ok) {
            fail("This port needs your own copy of the MS-DOS game.\n\n" + g.message +
                 "\n\nExtract the original game files (DAVE.EXE, EGATILES.DD2, LEVEL01.DD2 ...) into:\n" +
                 dd2::path_utf8(game_dir) + "\nthen start DDHM Launcher.");
            return 1;
        }
        try {
            dd2::extract_assets(dd2::path_utf8(game_dir), dd2::path_utf8(data_dir));
        } catch (const std::exception& e) {
            fail(std::string("Could not extract the assets: ") + e.what());
            return 1;
        }
    }

    ddhm::Assets assets;
    try {
        assets = ddhm::load_assets(dd2::path_utf8(data_dir));
    } catch (const std::exception& e) {
        fail(e.what());
        return 1;
    }

    // Keys and high score: CTLPANEL.DD2 in the preference folder (imported once from the game folder if present).
    std::string panel_path;
    if (!pref.empty()) {
        panel_path = dd2::path_utf8(pref / "CTLPANEL.DD2");
        std::error_code ec;
        if (!fs::exists(pref / "CTLPANEL.DD2", ec) && fs::exists(game_dir / "CTLPANEL.DD2", ec))
            fs::copy_file(game_dir / "CTLPANEL.DD2", pref / "CTLPANEL.DD2", ec);
    }

    SDL_Window* window = SDL_CreateWindow("Dangerous Dave in the Haunted Mansion", 960, 720, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) { fail(std::string("Cannot open a window: ") + SDL_GetError()); return 1; }
    set_icon(window, assets);
    SDL_SetRenderVSync(renderer, 1);
    auto apply_presentation = [&] {
        // 320x200 on a 4:3 screen: pixels are 1.2 times taller than wide. Square pixels: integer scaling only.
        if (settings.square_pixels)
            SDL_SetRenderLogicalPresentation(renderer, 320, 200, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
        else
            SDL_SetRenderLogicalPresentation(renderer, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    };
    apply_presentation();
    SDL_SetWindowFullscreen(window, settings.fullscreen);
    SDL_HideCursor();
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 320, 200);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    const int kRate = 44100;
    SDL_AudioSpec spec{SDL_AUDIO_S16, 1, kRate};
    SDL_AudioStream* audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (audio) SDL_ResumeAudioStreamDevice(audio);
    ddhm::Speaker speaker(kRate);
    speaker.soft = settings.soft_sound;
    std::vector<int16_t> pcm;

    SDL_Gamepad* pad = nullptr;
    auto open_pad = [&] {
        if (pad) return;
        int n = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) {
            if (n > 0) pad = SDL_OpenGamepad(ids[0]);
            SDL_free(ids);
        }
    };
    open_pad();

    ddhm::Machine machine(assets.exe);
    ddhm::GameOptions opt;
    opt.ctlpanel_path = panel_path;
    machine.start([&](ddhm::Machine& m) {
        ddhm::Game game(m, assets, opt);
        game.run();
    });

    const double frame_ns = 1e9 / 70.086;            // VGA mode 0Dh refresh: the game's retrace rate
    Uint64 last = SDL_GetTicksNS();
    double acc = 0;
    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_EVENT_QUIT: running = false; break;
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP: {
                    const bool down = ev.type == SDL_EVENT_KEY_DOWN;
                    if (down && !ev.key.repeat) {
                        if (ev.key.scancode == SDL_SCANCODE_F11 ||
                            (ev.key.scancode == SDL_SCANCODE_RETURN && (ev.key.mod & SDL_KMOD_ALT))) {
                            settings.fullscreen = !settings.fullscreen;
                            SDL_SetWindowFullscreen(window, settings.fullscreen);
                            break;
                        }
                        if (ev.key.scancode == SDL_SCANCODE_F8) {
                            settings.soft_sound = speaker.soft = !speaker.soft;
                            break;
                        }
                        if (ev.key.scancode == SDL_SCANCODE_F9) {
                            settings.square_pixels = !settings.square_pixels;
                            apply_presentation();
                            break;
                        }
                    }
                    // Auto-repeat is passed on: the PC keyboard repeats make codes too, which matters after the game
                    // clears its key table (0x78DF) while a key is held.
                    if (const int sc = pc_scancode(ev.key.scancode)) machine.key_event(sc, down);
                    break;
                }
                case SDL_EVENT_GAMEPAD_ADDED: open_pad(); break;
                case SDL_EVENT_GAMEPAD_REMOVED:
                    if (pad && ev.gdevice.which == SDL_GetGamepadID(pad)) { SDL_CloseGamepad(pad); pad = nullptr; open_pad(); }
                    break;
                case SDL_EVENT_WINDOW_FOCUS_LOST: machine.keydown.fill(0); break;   // no stuck keys after Alt+Tab
                default: break;
            }
        }
        machine.pad_connected = pad != nullptr;
        if (pad) {
            const int ax = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ay = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
            int dx = ax < -16000 ? -1 : (ax > 16000 ? 1 : 0), dy = ay < -16000 ? -1 : (ay > 16000 ? 1 : 0);
            if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) dx = -1;
            if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) dx = 1;
            if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) dy = -1;
            if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) dy = 1;
            static const int kDir[9] = {7, 0, 1, 6, 8, 2, 5, 4, 3};
            machine.pad_dir = kDir[dy * 3 + dx + 4];
            machine.pad_b1 = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH);
            machine.pad_b2 = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST) || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST);
        }

        const Uint64 now = SDL_GetTicksNS();
        acc += double(now - last);
        last = now;
        if (acc > frame_ns * 8) acc = frame_ns * 8;   // after a stall, catch up a little, not forever
        bool ran = false;
        while (acc >= frame_ns && running) {
            acc -= frame_ns;
            if (!machine.run_frame()) { running = false; break; }
            ran = true;
            for (uint16_t d : machine.speaker) speaker.tick(d, pcm);
            machine.speaker.clear();
        }
        if (audio && !pcm.empty()) {
            if (SDL_GetAudioStreamQueued(audio) < kRate / 5 * 2)  // keep latency under ~0.2 s
                SDL_PutAudioStreamData(audio, pcm.data(), int(pcm.size() * sizeof(int16_t)));
            pcm.clear();
        }
        if (ran) {
            const auto img = machine.screen.rgba();
            SDL_UpdateTexture(tex, nullptr, img.data(), 320 * 4);
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (settings.square_pixels) SDL_RenderTexture(renderer, tex, nullptr, nullptr);
        else { const SDL_FRect dst{0, 0, 320, 240}; SDL_RenderTexture(renderer, tex, nullptr, &dst); }
        SDL_RenderPresent(renderer);
        if (!ran) SDL_Delay(1);
    }
    if (!pref.empty()) settings.save(pref / "ddhm.ini");
    if (pad) SDL_CloseGamepad(pad);
    if (audio) SDL_DestroyAudioStream(audio);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
