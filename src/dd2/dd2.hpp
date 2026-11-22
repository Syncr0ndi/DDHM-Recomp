// SPDX-License-Identifier: MIT
// dd2.hpp - asset decoding for "Dangerous Dave in the Haunted Mansion" (.DD2 files).
// Pure C++20, no platform dependencies, so it builds identically on Windows 11 / MSVC.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace dd2 {

using Bytes = std::vector<uint8_t>;

struct Error : std::runtime_error { using std::runtime_error::runtime_error; };

// Paths are UTF-8 everywhere in the port (converted to the platform's form when files are opened).
std::filesystem::path utf8_path(const std::string& utf8);
std::string path_utf8(const std::filesystem::path& p);
Bytes read_file(const std::string& path);

// ---- Compression ---------------------------------------------------------
// Word-oriented RLE. A word equal to `tag` is followed by (count, value) words.
Bytes rlew_expand(const Bytes& src, size_t start, uint16_t tag, size_t out_bytes);

// "HUFF" container: u32 expanded length, 255 nodes (bit0,bit1 as u16 pairs), then
// an LSB-first bitstream. Child value < 0x100 = literal byte, >= 0x100 = node (v-0x100).
// Root is node 254.
Bytes huff_expand(const Bytes& file);

// ---- Graphics ------------------------------------------------------------
struct Rgb { uint8_t r, g, b; };
extern const std::array<Rgb, 16> kEgaPalette;

constexpr int kTileSize = 16;                 // pixels
constexpr int kTileCount = 858;
constexpr size_t kTileBytes = 4 * 32;         // 4 planes x 16 rows x 2 bytes

struct TileSet {
    int count = 0;
    std::vector<uint8_t> pixels;              // count * 256 palette indices (0..15)
    const uint8_t* tile(int i) const { return &pixels[size_t(i) * 256]; }
};
TileSet load_tileset(const std::string& path);   // EGATILES.DD2

struct Picture {                                  // 'PIC' planar EGA image (raw or HUFF-wrapped)
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;                  // palette indices
};
Picture load_picture(const std::string& path);   // TITLE1/2, STARPIC, PROGPIC

// Byte RLE (loader 0x7B04, expander 0x7EFC): u32 expanded length, then bytes; 0xFE is followed by (count, value).
Bytes rleb_expand(const Bytes& file);
// INTRO.DD2: RLEB-packed, four 32x64-byte colour planes = the 256x64 "Gamer's Edge presents" logo. Only the intro
// routine at 0x6A28 reads it, and nothing in DAVE.EXE 1.12 calls that routine, so the game never shows it.
Picture load_intro(const std::string& path);

// ---- Sprites ---------------------------------------------------------------
// Sprite files (S_DAVE, S_CHUNK1, S_CHUNK2, S_FRANK, S_MASTER .DD2) are HUFF-wrapped:
//   u16 plane_size, then 5 planes of plane_size bytes: 4 EGA colour planes (B,G,R,I) + 1 mask plane
//   (mask bit 1 = transparent). Inside every plane the sprites follow each other in table order, each
//   taking width_bytes*height bytes, rows top to bottom, MSB = leftmost pixel.
// The per-sprite names and sizes are not stored in the data files; they live in the DOS executable and are read
// from the user's DAVE.EXE at run time (install_exe_tables), never stored in this project.
constexpr int kSpriteCount = 199;
constexpr int kSpriteFileCount = 5;
extern const char* const kSpriteFileNames[kSpriteFileCount];   // "S_DAVE", ...

struct SpriteInfo {
    const char* name;
    uint8_t file;                // index into kSpriteFileNames (set by load_sprites: the table fills the files in order)
    uint8_t width_bytes;         // width / 8
    uint16_t height;
    uint16_t f4, f6;                   // raw record words; meaning not yet decoded
    int16_t hit_left, hit_top, hit_right, hit_bottom;   // hit-box offsets from the object origin, 1/16 pixel
    int16_t draw_dx, draw_dy;          // sprite draw offset from the object origin, 1/16 pixel (also added to the hit-box)
};
extern const SpriteInfo* kSpriteTable;              // kSpriteCount entries once install_exe_tables() ran

// ---- Tables from DAVE.EXE's data segment -----------------------------------------------------------------
// Sprite records (image 0x12580), object state records (DS:0x08BC..0x1384), tile info (DS:0x140C), the random table
// (DS:0x3024) and smaller tables are not compiled into the port: they are read from the user's own executable.
// load_exe() installs them; everything that needs them (sprites, levels' collision, the game) runs after that.
void install_exe_tables(const Bytes& image);        // unpacked DAVE.EXE load image (validated by load_exe)
bool exe_tables_installed();
uint32_t exe_tables_generation();                   // changes on every install (for caches built from the tables)
uint8_t ds_u8(uint16_t ds_offset);                  // DGROUP reads; throw dd2::Error before installation
int16_t ds_s16(uint16_t ds_offset);
uint16_t ds_u16(uint16_t ds_offset);

struct Sprite {
    const SpriteInfo* info = nullptr;
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;       // width*height; EGA colour 0..15, or kTransparent
    static constexpr uint8_t kTransparent = 0xFF;
};
// Loads all 199 sprites from the five S_*.DD2 files found in `dir` (with trailing slash).
std::vector<Sprite> load_sprites(const std::string& dir);
std::vector<uint8_t> sprite_to_rgba(const Sprite&);   // transparent pixels get alpha 0

// ---- Levels --------------------------------------------------------------
struct Level {
    int width = 0, height = 0, layers = 0;
    int layer_stride = 0;                          // in cells; may exceed width*height (LEVEL06)
    std::array<uint16_t, 16> header{};             // raw header words, kept for reference
    std::vector<uint16_t> tiles;                   // layer 0: tile indices into TileSet
    std::vector<uint16_t> entities;                // layer 1: spawn/object codes (raw, see docs)
    uint16_t tile_at(int x, int y) const { return tiles[size_t(y) * width + x]; }
    uint16_t entity_at(int x, int y) const { return entities[size_t(y) * width + x]; }
};
Level load_level(const std::string& path);       // LEVELnn.DD2

// ---- Entities (level layer 1) ----------------------------------------------
// The DOS game scans layer 1 at level start (routine at image 0x5706) and creates one object per recognised
// code. Positions are fixed point: 256 units per tile = 16 units per pixel.
struct SpawnDef {
    uint16_t code;            // value found in layer 1
    uint8_t kind;             // object kind passed to the game's object constructor (0x19A8)
    int8_t dir;               // initial facing: -1 = west/left, +1 = east/right
    int16_t y_off;            // added to tile_y*256 by the spawn handler
    uint16_t state_ptr;       // DS offset of the initial state record
    uint16_t sprite_west;     // sprite index used when facing west (from that state record, filled at install)
    uint16_t sprite_east;     // sprite index used when facing east
};
const SpawnDef* find_spawn_def(uint16_t code);   // nullptr when the code is not a spawn code
const char* spawn_label(const SpawnDef&);        // e.g. "ZOMBIE", "FRANK"

struct Spawn {
    const SpawnDef* def = nullptr;
    int tile_x = 0, tile_y = 0;
    int px = 0, py = 0;       // top-left pixel of the sprite at spawn time
    int sprite = 0;           // sprite index to draw (0 = invisible placeholder, e.g. delayed ghosts)
};
// Layer-1 values that are not spawn codes. Observed behaviour (from the player tile-touch routine at 0x4729):
// they only matter on tiles whose property byte is 1 or 2. Values 0x0100-0x7FFF look like (x<<8)|y door
// destinations; 0x8000-0x8005 are flagged triggers. Semantics provisional.
struct Trigger { int tile_x, tile_y; uint16_t value; };

struct EntityList {
    std::vector<Spawn> spawns;
    std::vector<Trigger> triggers;
    int player_index = -1;    // index into spawns, or -1
};
EntityList extract_entities(const Level&);

// Alpha-blits a sprite into an RGBA image (clipped).
void blit_sprite_rgba(std::vector<uint8_t>& img, int img_w, int img_h, const Sprite&, int x, int y);

// ---- Tile behaviour (collision) ---------------------------------------------
// The DOS game ships a "tile info" block (DS:0x140C) with one byte per tile in six tables. Four are collision
// faces, one is a property code, one is unused (all zero). Collision resolver: image 0x1BA2 / 0x1C7D.
enum class TileFace { Top, Bottom, Left, Right };
enum TileProperty : uint8_t {
    kPropNone = 0,
    kPropDoor = 1,          // door; layer-1 value on the tile is the destination / exit (see ENTITIES.md)
    kPropSearch = 2,        // door-like tile handled without the open animation (layer-1 value read the same way)
    kPropHazard = 0x0D,     // touching it sets the level-state variable to -8 (death)
    kPropExitZone = 0x63,   // touching it sets the level-state variable to 1 (level complete)
};
// True when the given face of `tile` is solid. Top = floor (blocks downward movement), Bottom = ceiling,
// Left face blocks an object moving right, Right face blocks an object moving left.
bool tile_face_solid(int tile, TileFace face);
uint8_t tile_property(int tile);
int tile_count_with_face(TileFace face);

// Transparent RGBA overlay showing collision faces (coloured 2px edges) and special properties (tints).
std::vector<uint8_t> render_collision_overlay_rgba(const Level&);

// ---- Rendering helpers ---------------------------------------------------
// RGBA8 (R,G,B,A byte order) image of the whole tile layer.
std::vector<uint8_t> render_level_rgba(const Level&, const TileSet&);
std::vector<uint8_t> picture_to_rgba(const Picture&);

// ---- PNG output (uncompressed deflate; no zlib dependency) ----------------
void write_png_rgba(const std::string& path, int w, int h, const uint8_t* rgba);

}  // namespace dd2
