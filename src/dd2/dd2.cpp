// SPDX-License-Identifier: MIT
#include "dd2.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace dd2 {

const std::array<Rgb, 16> kEgaPalette = {{
    {0, 0, 0},     {0, 0, 170},   {0, 170, 0},   {0, 170, 170},
    {170, 0, 0},   {170, 0, 170}, {170, 85, 0},  {170, 170, 170},
    {85, 85, 85},  {85, 85, 255}, {85, 255, 85}, {85, 255, 255},
    {255, 85, 85}, {255, 85, 255},{255, 255, 85},{255, 255, 255}}};

std::filesystem::path utf8_path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string path_utf8(const std::filesystem::path& p) { const std::u8string u = p.u8string(); return std::string(u.begin(), u.end()); }

Bytes read_file(const std::string& path) {
    std::ifstream f(utf8_path(path), std::ios::binary);
    if (!f) throw Error("cannot open " + path);
    return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

static uint16_t u16(const Bytes& b, size_t o) {
    if (o + 2 > b.size()) throw Error("unexpected end of data");
    return uint16_t(b[o] | (b[o + 1] << 8));
}
static uint32_t u32(const Bytes& b, size_t o) { return u16(b, o) | (uint32_t(u16(b, o + 2)) << 16); }

Bytes rlew_expand(const Bytes& src, size_t start, uint16_t tag, size_t out_bytes) {
    if (out_bytes & 1) throw Error("RLEW output length must be even");
    Bytes out;
    out.reserve(out_bytes);
    size_t p = start;
    while (out.size() < out_bytes) {
        uint16_t w = u16(src, p);
        p += 2;
        if (w == tag) {
            uint16_t count = u16(src, p), value = u16(src, p + 2);
            p += 4;
            for (unsigned i = 0; i < count; ++i) {
                out.push_back(uint8_t(value & 0xff));
                out.push_back(uint8_t(value >> 8));
            }
        } else {
            out.push_back(uint8_t(w & 0xff));
            out.push_back(uint8_t(w >> 8));
        }
    }
    if (out.size() != out_bytes) throw Error("RLEW run overruns expected length");
    return out;
}

Bytes huff_expand(const Bytes& f) {
    constexpr size_t kNodes = 255, kTable = 8 + kNodes * 4;
    if (f.size() < kTable || std::memcmp(f.data(), "HUFF", 4) != 0) throw Error("not a HUFF file");
    const uint32_t n = u32(f, 4);
    uint16_t nodes[kNodes][2];
    for (size_t i = 0; i < kNodes; ++i) {
        nodes[i][0] = u16(f, 8 + i * 4);
        nodes[i][1] = u16(f, 10 + i * 4);
    }
    Bytes out;
    out.reserve(n);
    size_t p = kTable;
    unsigned node = 254;
    while (out.size() < n) {
        if (p >= f.size()) throw Error("HUFF stream truncated");
        const uint8_t byte = f[p++];
        for (int bit = 0; bit < 8 && out.size() < n; ++bit) {
            const uint16_t next = nodes[node][(byte >> bit) & 1];
            if (next < 0x100) {
                out.push_back(uint8_t(next));
                node = 254;
            } else {
                node = next - 0x100u;
                if (node >= kNodes) throw Error("HUFF node index out of range");
            }
        }
    }
    return out;
}

TileSet load_tileset(const std::string& path) {
    const Bytes b = read_file(path);
    if (b.empty() || b.size() % kTileBytes) throw Error("EGATILES size is not a multiple of 128");
    TileSet ts;
    ts.count = int(b.size() / kTileBytes);
    ts.pixels.resize(size_t(ts.count) * 256);
    for (int i = 0; i < ts.count; ++i)
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                int c = 0;
                for (int pl = 0; pl < 4; ++pl)
                    c |= ((b[i * kTileBytes + pl * 32 + y * 2 + (x >> 3)] >> (7 - (x & 7))) & 1) << pl;
                ts.pixels[size_t(i) * 256 + y * 16 + x] = uint8_t(c);
            }
    return ts;
}

Picture load_picture(const std::string& path) {
    Bytes b = read_file(path);
    if (b.size() >= 4 && std::memcmp(b.data(), "HUFF", 4) == 0) b = huff_expand(b);
    if (b.size() < 8 || std::memcmp(b.data(), "PIC", 3) != 0) throw Error("not a PIC image: " + path);
    const int bpr = u16(b, 4), h = u16(b, 6);
    const size_t plane = size_t(bpr) * h;
    if (b.size() < 8 + 4 * plane) throw Error("PIC data truncated");
    Picture pic;
    pic.width = bpr * 8;
    pic.height = h;
    pic.pixels.resize(size_t(pic.width) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < pic.width; ++x) {
            int c = 0;
            for (int pl = 0; pl < 4; ++pl)
                c |= ((b[8 + pl * plane + size_t(y) * bpr + (x >> 3)] >> (7 - (x & 7))) & 1) << pl;
            pic.pixels[size_t(y) * pic.width + x] = uint8_t(c);
        }
    return pic;
}

Bytes rleb_expand(const Bytes& f) {
    if (f.size() < 4) throw Error("RLEB file too short");
    const size_t n = u32(f, 0);
    if (n > (1u << 24)) throw Error("RLEB length implausible");
    Bytes out;
    out.reserve(n);
    size_t i = 4;
    while (out.size() < n) {
        if (i >= f.size()) throw Error("RLEB data truncated");
        const uint8_t c = f[i++];
        if (c != 0xFE) { out.push_back(c); continue; }
        if (i + 2 > f.size()) throw Error("RLEB data truncated");
        const uint8_t count = f[i], value = f[i + 1];
        i += 2;
        for (int k = 0; k < count; ++k) out.push_back(value);   // (count 0 writes nothing, like 0x7F58)
    }
    out.resize(n);
    return out;
}

Picture load_intro(const std::string& path) {
    const Bytes b = rleb_expand(read_file(path));
    constexpr int kBpr = 32, kH = 64;
    constexpr size_t kPlane = size_t(kBpr) * kH;
    if (b.size() < 4 * kPlane) throw Error("INTRO data too short");
    Picture pic;
    pic.width = kBpr * 8;
    pic.height = kH;
    pic.pixels.resize(size_t(pic.width) * kH);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < pic.width; ++x) {
            int c = 0;
            for (int pl = 0; pl < 4; ++pl) c |= ((b[pl * kPlane + size_t(y) * kBpr + size_t(x >> 3)] >> (7 - (x & 7))) & 1) << pl;
            pic.pixels[size_t(y) * size_t(pic.width) + size_t(x)] = uint8_t(c);
        }
    return pic;
}

const char* const kSpriteFileNames[kSpriteFileCount] = {"S_DAVE", "S_CHUNK1", "S_CHUNK2", "S_FRANK", "S_MASTER"};

void set_sprite_file(int sprite, int file);   // tables.cpp

std::vector<Sprite> load_sprites(const std::string& dir) {
    std::vector<Sprite> all;
    all.reserve(kSpriteCount);
    int next = 0;
    for (int f = 0; f < kSpriteFileCount; ++f) {
        const Bytes d = huff_expand(read_file(dir + kSpriteFileNames[f] + ".DD2"));
        const size_t plane = u16(d, 0);
        if (d.size() != 2 + 5 * plane) throw Error(std::string("unexpected sprite file size: ") + kSpriteFileNames[f]);
        if (!exe_tables_installed()) throw Error("sprite table not loaded (DAVE.EXE)");
        size_t run = 0;
        // The sprite table only lists sizes; the sprites fill the five files in table order, each file exactly.
        for (; next < kSpriteCount && run + size_t(kSpriteTable[next].width_bytes) * kSpriteTable[next].height <= plane; ++next) {
            const SpriteInfo& si = kSpriteTable[next];
            const size_t bytes = size_t(si.width_bytes) * si.height;
            set_sprite_file(next, f);
            Sprite sp;
            sp.info = &si;
            sp.width = si.width_bytes * 8;
            sp.height = si.height;
            sp.pixels.resize(size_t(sp.width) * sp.height);
            for (int y = 0; y < sp.height; ++y)
                for (int x = 0; x < sp.width; ++x) {
                    const size_t idx = run + size_t(y) * si.width_bytes + (x >> 3);
                    const int bit = 7 - (x & 7);
                    if ((d[2 + 4 * plane + idx] >> bit) & 1) { sp.pixels[size_t(y) * sp.width + x] = Sprite::kTransparent; continue; }
                    int c = 0;
                    for (int pl = 0; pl < 4; ++pl) c |= ((d[2 + pl * plane + idx] >> bit) & 1) << pl;
                    sp.pixels[size_t(y) * sp.width + x] = uint8_t(c);
                }
            all.push_back(std::move(sp));
            run += bytes;
        }
        if (run != plane) throw Error(std::string("sprite sizes do not fill plane: ") + kSpriteFileNames[f]);
    }
    if (int(all.size()) != kSpriteCount) throw Error("sprite count mismatch");
    return all;
}

std::vector<uint8_t> sprite_to_rgba(const Sprite& s) {
    std::vector<uint8_t> img(s.pixels.size() * 4, 0);
    for (size_t i = 0; i < s.pixels.size(); ++i) {
        if (s.pixels[i] == Sprite::kTransparent) continue;
        const Rgb c = kEgaPalette[s.pixels[i] & 15];
        img[i * 4] = c.r; img[i * 4 + 1] = c.g; img[i * 4 + 2] = c.b; img[i * 4 + 3] = 255;
    }
    return img;
}

Level load_level(const std::string& path) {
    const Bytes f = read_file(path);
    const uint32_t len = u32(f, 0);
    if (len < 32 || (len & 1)) throw Error("bad level length");
    const Bytes d = rlew_expand(f, 4, 0xFEFE, len);
    Level lv;
    for (int i = 0; i < 16; ++i) lv.header[i] = u16(d, i * 2);
    lv.width = lv.header[0];
    lv.height = lv.header[1];
    lv.layers = lv.header[2];
    lv.layer_stride = lv.header[7] / 2;
    if (lv.width <= 0 || lv.height <= 0 || lv.layers < 2) throw Error("bad level header");
    if (lv.layer_stride < lv.width * lv.height) throw Error("level layer stride too small");
    if (len != 32u + uint32_t(lv.layers) * lv.header[7]) throw Error("level length/header mismatch");
    auto read_layer = [&](int layer, std::vector<uint16_t>& dst) {
        dst.resize(size_t(lv.width) * lv.height);
        const size_t base = 32 + size_t(layer) * lv.header[7];
        for (size_t i = 0; i < dst.size(); ++i) dst[i] = u16(d, base + i * 2);
    };
    read_layer(0, lv.tiles);
    read_layer(1, lv.entities);
    return lv;
}

const char* spawn_label(const SpawnDef& d) {
    switch (d.code) {
        case 0x01: return "ZOMBIE";    case 0x02: return "HUNCHBACK"; case 0x03: return "SLIME";
        case 0x04: return "GHOST";     case 0x05: return "GHOST (delayed)"; case 0x06: return "SPIDER";
        case 0x07: return "FLAMING SKULL"; case 0x09: return "WOLF";   case 0x0B: return "FRANK";
        case 0x0C: return "MASTER";    case 0x0D: return "DELBERT";   case 0xFF: return "PLAYER START";
        default: return (d.code >= 0x0E && d.code <= 0x13) ? "PICKUP" : "?";
    }
}

EntityList extract_entities(const Level& lv) {
    EntityList out;
    for (int ty = 0; ty < lv.height; ++ty)
        for (int tx = 0; tx < lv.width; ++tx) {
            const uint16_t v = lv.entity_at(tx, ty);
            if (!v) continue;
            const SpawnDef* d = find_spawn_def(v);
            if (!d) { out.triggers.push_back({tx, ty, v}); continue; }
            Spawn s;
            s.def = d;
            s.tile_x = tx;
            s.tile_y = ty;
            s.sprite = d->dir > 0 ? d->sprite_east : d->sprite_west;
            const SpriteInfo& si = kSpriteTable[s.sprite];
            s.px = (tx * 256 + si.draw_dx) >> 4;                       // floor division by 16
            s.py = (ty * 256 + d->y_off + si.draw_dy) >> 4;
            if (d->code == 0xFF) out.player_index = int(out.spawns.size());
            out.spawns.push_back(s);
        }
    return out;
}

void blit_sprite_rgba(std::vector<uint8_t>& img, int img_w, int img_h, const Sprite& sp, int x, int y) {
    for (int sy = 0; sy < sp.height; ++sy) {
        const int dy = y + sy;
        if (dy < 0 || dy >= img_h) continue;
        for (int sx = 0; sx < sp.width; ++sx) {
            const int dx = x + sx;
            if (dx < 0 || dx >= img_w) continue;
            const uint8_t p = sp.pixels[size_t(sy) * sp.width + sx];
            if (p == Sprite::kTransparent) continue;
            const Rgb c = kEgaPalette[p & 15];
            uint8_t* o = &img[(size_t(dy) * img_w + dx) * 4];
            o[0] = c.r; o[1] = c.g; o[2] = c.b; o[3] = 255;
        }
    }
}


bool tile_face_solid(int tile, TileFace face) {
    if (tile < 0 || tile >= 858) return false;
    // Tile info block DS:0x140C: u16 count, u16 tables, u16 offset[6] (from 0x140C) of 858-byte tables:
    // 0 top, 1 bottom, 2 right face, 3 left face, 4 unused, 5 property.
    static const int kTable[4] = {0, 1, 3, 2};          // TileFace::Top, Bottom, Left, Right
    const uint16_t table = uint16_t(0x140C + ds_u16(uint16_t(0x140C + 4 + 2 * kTable[int(face)])));
    return ds_u8(uint16_t(table + tile)) != 0;
}

uint8_t tile_property(int tile) {
    if (tile < 0 || tile >= 858) return 0;
    return ds_u8(uint16_t(0x140C + ds_u16(0x140C + 4 + 2 * 5) + tile));
}

int tile_count_with_face(TileFace face) {
    int n = 0;
    for (int i = 0; i < 858; ++i) n += tile_face_solid(i, face) ? 1 : 0;
    return n;
}

std::vector<uint8_t> render_collision_overlay_rgba(const Level& lv) {
    const int W = lv.width * 16, H = lv.height * 16;
    std::vector<uint8_t> img(size_t(W) * H * 4, 0);
    auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        uint8_t* o = &img[(size_t(y) * W + x) * 4];
        o[0] = r; o[1] = g; o[2] = b; o[3] = a;
    };
    for (int ty = 0; ty < lv.height; ++ty)
        for (int tx = 0; tx < lv.width; ++tx) {
            const int id = lv.tile_at(tx, ty);
            const int x0 = tx * 16, y0 = ty * 16;
            const uint8_t pr = tile_property(id);
            if (pr) {                                              // tint whole tile
                uint8_t r = 255, g = 255, b = 255;
                if (pr == kPropDoor || pr == kPropSearch) { r = 60; g = 120; b = 255; }
                else if (pr == kPropHazard) { r = 255; g = 40; b = 40; }
                else if (pr == kPropExitZone) { r = 255; g = 255; b = 255; }
                else { r = 255; g = 160; b = 0; }
                for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) put(x0 + x, y0 + y, r, g, b, 90);
            }
            for (int i = 0; i < 16; ++i) {
                for (int k = 0; k < 2; ++k) {
                    if (tile_face_solid(id, TileFace::Top)) put(x0 + i, y0 + k, 0, 255, 0, 255);
                    if (tile_face_solid(id, TileFace::Bottom)) put(x0 + i, y0 + 15 - k, 0, 255, 255, 255);
                    if (tile_face_solid(id, TileFace::Left)) put(x0 + k, y0 + i, 255, 255, 0, 255);
                    if (tile_face_solid(id, TileFace::Right)) put(x0 + 15 - k, y0 + i, 255, 0, 255, 255);
                }
            }
        }
    return img;
}

std::vector<uint8_t> render_level_rgba(const Level& lv, const TileSet& ts) {
    const int W = lv.width * 16, H = lv.height * 16;
    std::vector<uint8_t> img(size_t(W) * H * 4, 255);
    for (int ty = 0; ty < lv.height; ++ty)
        for (int tx = 0; tx < lv.width; ++tx) {
            const int id = lv.tile_at(tx, ty);
            if (id >= ts.count) continue;
            const uint8_t* t = ts.tile(id);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    const Rgb c = kEgaPalette[t[y * 16 + x]];
                    uint8_t* o = &img[((size_t(ty) * 16 + y) * W + tx * 16 + x) * 4];
                    o[0] = c.r; o[1] = c.g; o[2] = c.b; o[3] = 255;
                }
        }
    return img;
}

std::vector<uint8_t> picture_to_rgba(const Picture& p) {
    std::vector<uint8_t> img(p.pixels.size() * 4);
    for (size_t i = 0; i < p.pixels.size(); ++i) {
        const Rgb c = kEgaPalette[p.pixels[i] & 15];
        img[i * 4] = c.r; img[i * 4 + 1] = c.g; img[i * 4 + 2] = c.b; img[i * 4 + 3] = 255;
    }
    return img;
}

// ---- PNG writer (stored deflate blocks) ------------------------------------
static uint32_t crc32(const uint8_t* d, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}
static void put32be(Bytes& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s));
}
static void put_chunk(Bytes& out, const char* type, const Bytes& data) {
    put32be(out, uint32_t(data.size()));
    Bytes td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    put32be(out, crc32(td.data(), td.size()));
}

void write_png_rgba(const std::string& path, int w, int h, const uint8_t* rgba) {
    Bytes raw;
    raw.reserve(size_t(h) * (size_t(w) * 4 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + size_t(y) * w * 4, rgba + size_t(y + 1) * w * 4);
    }
    Bytes z{0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    size_t pos = 0;
    do {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n & 0xff));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n & 0xff));
        z.push_back(uint8_t((~n >> 8) & 0xff));
        z.insert(z.end(), raw.begin() + long(pos), raw.begin() + long(pos + n));
        pos += n;
        if (last) break;
    } while (true);
    put32be(z, (b << 16) | a);

    Bytes png{137, 80, 78, 71, 13, 10, 26, 10};
    Bytes ihdr;
    put32be(ihdr, uint32_t(w));
    put32be(ihdr, uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    put_chunk(png, "IHDR", ihdr);
    put_chunk(png, "IDAT", z);
    put_chunk(png, "IEND", {});
    std::ofstream f(utf8_path(path), std::ios::binary);
    if (!f) throw Error("cannot write " + path);
    f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
}

}  // namespace dd2
