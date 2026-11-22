// SPDX-License-Identifier: MIT
// tables.cpp - the game's data tables, read from the user's own DAVE.EXE at run time (see dd2.hpp).
// Only addresses and record layouts are written here; the contents come from the executable.
#include <memory>
#include <string>
#include <vector>

#include "dd2/dd2.hpp"
#include "dd2/exe.hpp"

namespace dd2 {

namespace {
struct Installed {
    Bytes image;
    std::vector<std::string> names;
    std::vector<SpriteInfo> sprites;
};
std::unique_ptr<Installed> g_tables;
uint32_t g_generation = 0;

constexpr size_t kSpriteRecords = 0x12580;   // 4 records of 32 bytes per sprite (the original + 3 pre-shifted copies)

// Spawn handlers (0x5706..): layer-1 code -> object kind, facing, y offset and first state. These are constants of the
// spawn code itself; the sprites are looked up in the state records at install time.
SpawnDef g_spawn[] = {
    {0x01, 2, -1, -128, 0xDD0, 0, 0},   // zombie
    {0x02, 3, -1, 128, 0xE84, 0, 0},    // hunchback
    {0x03, 4, 1, 128, 0xF4C, 0, 0},     // slime
    {0x04, 6, 1, 128, 0x1064, 0, 0},    // ghost
    {0x05, 6, 1, 128, 0xFEC, 0, 0},     // ghost, appears later
    {0x06, 5, 1, 0, 0x1140, 0, 0},      // spider
    {0x07, 8, 1, 0, 0x1118, 0, 0},      // flaming skull
    {0x09, 7, 1, 0, 0x10A0, 0, 0},      // wolf
    {0x0B, 10, -1, 0, 0x12BC, 0, 0},    // Frank
    {0x0C, 11, 1, 0, 0x1190, 0, 0},     // the Master
    {0x0D, 12, 1, 0, 0x1258, 0, 0},     // Delbert
    {0x0E, 9, -1, 0, 0xBF0, 0, 0},      // pickups 1..6
    {0x0F, 9, -1, 0, 0xC18, 0, 0},
    {0x10, 9, -1, 0, 0xC40, 0, 0},
    {0x11, 9, -1, 0, 0xC68, 0, 0},
    {0x12, 9, -1, 0, 0xC90, 0, 0},
    {0x13, 9, -1, 0, 0xCB8, 0, 0},
    {0xFF, 1, 1, 15, 0x8BC, 0, 0},      // Dave's start
};

uint16_t rd16(const Bytes& b, size_t o) {
    if (o + 2 > b.size()) throw Error("table read past the end of DAVE.EXE");
    return uint16_t(b[o] | (b[o + 1] << 8));
}
const Installed& need() {
    if (!g_tables) throw Error("game tables not loaded (DAVE.EXE)");
    return *g_tables;
}
}  // namespace

const SpriteInfo* kSpriteTable = nullptr;

void install_exe_tables(const Bytes& image) {
    auto t = std::make_unique<Installed>();
    t->image = image;
    t->names.reserve(kSpriteCount);
    t->sprites.reserve(kSpriteCount);
    for (int i = 0; i < kSpriteCount; ++i) {
        const size_t r = kSpriteRecords + size_t(i) * 128;
        std::string name;
        for (size_t k = 0x10; k < 0x1C && r + k < image.size() && image[r + k]; ++k) name.push_back(char(image[r + k]));
        t->names.push_back(name);
        SpriteInfo si{};
        si.file = 0;
        si.width_bytes = uint8_t(rd16(image, r + 0));
        si.height = rd16(image, r + 2);
        si.f4 = rd16(image, r + 4);
        si.f6 = rd16(image, r + 6);
        si.hit_left = int16_t(rd16(image, r + 8));
        si.hit_top = int16_t(rd16(image, r + 10));
        si.hit_right = int16_t(rd16(image, r + 12));
        si.hit_bottom = int16_t(rd16(image, r + 14));
        si.draw_dx = int16_t(rd16(image, r + 0x1C));
        si.draw_dy = int16_t(rd16(image, r + 0x1E));
        t->sprites.push_back(si);
    }
    for (int i = 0; i < kSpriteCount; ++i) t->sprites[size_t(i)].name = t->names[size_t(i)].c_str();
    for (SpawnDef& d : g_spawn) {                       // state record: +0 sprite west, +2 sprite east
        d.sprite_west = rd16(image, kDataSegment + d.state_ptr);
        d.sprite_east = rd16(image, kDataSegment + d.state_ptr + 2);
    }
    g_tables = std::move(t);
    kSpriteTable = g_tables->sprites.data();
    ++g_generation;
}

bool exe_tables_installed() { return g_tables != nullptr; }
uint32_t exe_tables_generation() { return g_generation; }

uint8_t ds_u8(uint16_t off) {
    const Bytes& im = need().image;
    if (kDataSegment + off >= im.size()) throw Error("table read past the end of DAVE.EXE");
    return im[kDataSegment + off];
}
uint16_t ds_u16(uint16_t off) { return rd16(need().image, kDataSegment + off); }
int16_t ds_s16(uint16_t off) { return int16_t(ds_u16(off)); }

void set_sprite_file(int sprite, int file) {
    if (g_tables && sprite >= 0 && sprite < kSpriteCount) g_tables->sprites[size_t(sprite)].file = uint8_t(file);
}

const SpawnDef* find_spawn_def(uint16_t code) {
    for (const SpawnDef& d : g_spawn)
        if (d.code == code) return &d;
    return nullptr;
}

}  // namespace dd2
