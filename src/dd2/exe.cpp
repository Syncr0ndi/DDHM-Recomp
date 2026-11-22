// SPDX-License-Identifier: MIT
#include "dd2/exe.hpp"

#include <cstring>

namespace dd2 {

namespace {
uint16_t rd16(const Bytes& b, size_t o) {
    if (o + 2 > b.size()) throw Error("read past end of executable image");
    return uint16_t(b[o] | (b[o + 1] << 8));
}
uint32_t rd32(const Bytes& b, size_t o) { return rd16(b, o) | (uint32_t(rd16(b, o + 2)) << 16); }

// Image offsets (Softdisk DAVE.EXE 1.12).
constexpr size_t kGfxHeader = 0x122B0;     // EGAGRAPH-style header (segment 0x122B, read at 0x07AE)
constexpr size_t kGfxPlanes = 0x18900;     // segment 0x1890: four colour planes of tile8 + picture data
constexpr size_t kFontBase = 0x106D0;      // segment 0x106D: fontstruct
constexpr size_t kSoundBase = 0x257C0;     // segment 0x257C: "SND" table + data
constexpr size_t kImageSize = 215360;
constexpr uint32_t kImageCrc = 0xE7C07BB3;   // CRC-32 of that image
}  // namespace

// LZEXE 0.91 decompressor (same algorithm as tools/unlzexe.c, without rebuilding an MZ header).
Bytes lzexe_unpack(const Bytes& in) {
    if (in.size() < 0x20 || rd16(in, 0) != 0x5A4D || std::memcmp(&in[0x1C], "LZ91", 4) != 0)
        throw Error("not an LZEXE 0.91 executable");
    const size_t hdr_paras = rd16(in, 8), cs = rd16(in, 0x16);
    const size_t cs_off = (cs + hdr_paras) << 4;
    const uint16_t packed_paras = rd16(in, cs_off + 8);
    size_t ip = ((cs - packed_paras + hdr_paras) << 4);
    auto getb = [&]() -> int {
        if (ip >= in.size()) throw Error("LZEXE stream ran off the end");
        return in[ip++];
    };
    uint16_t bits = 0;
    int count = 0;
    auto get16 = [&]() { const int a = getb(); const int b = getb(); return uint16_t(a | (b << 8)); };
    bits = get16();
    count = 16;
    auto getbit = [&]() {
        const int b = bits & 1;
        if (--count == 0) { bits = get16(); count = 16; } else bits >>= 1;
        return b;
    };
    Bytes out;
    out.reserve(0x40000);
    for (;;) {
        if (getbit()) { out.push_back(uint8_t(getb())); continue; }
        int len, span;
        if (!getbit()) {
            len = getbit() << 1;
            len |= getbit();
            len += 2;
            span = getb() | 0xFF00;
        } else {
            span = getb();
            len = getb();
            span |= ((len & ~0x07) << 5) | 0xE000;
            len = (len & 0x07) + 2;
            if (len == 2) {
                len = getb();
                if (len == 0) break;
                if (len == 1) continue;
                ++len;
            }
        }
        const int back = int16_t(uint16_t(span));
        if (size_t(-back) > out.size()) throw Error("LZEXE back-reference before start");
        for (; len > 0; --len) out.push_back(out[out.size() + size_t(back)]);
        if (out.size() > 0x100000) throw Error("LZEXE output too large");
    }
    return out;
}

int Font::text_width(const std::string& s) const {
    int w = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const uint8_t c = uint8_t(s[i]);
        if (c == 0x7F) { ++i; continue; }
        w += width[c];
    }
    return w;
}

std::string ExeData::ds_string(uint16_t off) const {
    std::string s;
    for (size_t o = kDataSegment + off; o < image.size() && image[o]; ++o) s.push_back(char(image[o]));
    return s;
}
uint16_t ExeData::ds_word(uint16_t off) const { return rd16(image, kDataSegment + off); }
uint8_t ExeData::ds_byte(uint16_t off) const { return image.at(kDataSegment + off); }

ExeData load_exe_from_dir(const std::string& dir) {
    std::string d = dir;
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    std::error_code ec;
    if (std::filesystem::exists(utf8_path(d + "DAVEDATA.BIN"), ec)) return load_exe(d + "DAVEDATA.BIN");
    return load_exe(d + "DAVE.EXE");
}

ExeData load_exe(const std::string& path) {
    ExeData d;
    const Bytes file = read_file(path);
    // DAVE.EXE itself, or DAVEDATA.BIN: its already unpacked load image as written by extract_assets().
    d.image = (file.size() >= 2 && file[0] == 'M' && file[1] == 'Z') ? lzexe_unpack(file) : file;
    // Identify the exact version by size and CRC-32 of the unpacked image (no text of the game is kept here).
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t b : d.image) {
        crc ^= b;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    if (d.image.size() != kImageSize || ~crc != kImageCrc)
        throw Error("unsupported DAVE.EXE (expected the Softdisk release, version 1.12)");
    const Bytes& im = d.image;
    install_exe_tables(im);

    // --- graphics header ---
    const uint32_t plane_size = rd32(im, kGfxHeader + 0x00);
    const size_t pic_table = kGfxHeader + rd16(im, kGfxHeader + 0x08);
    const int tile8_count = rd16(im, kGfxHeader + 0x10);
    const int pic_count = rd16(im, kGfxHeader + 0x28);
    const uint32_t pic_base = rd32(im, kGfxHeader + 0x2A);       // pictures follow the tile8 data in each plane
    auto pixel = [&](uint32_t off, int x, int y, int wbytes) {
        int v = 0;
        for (int p = 0; p < 4; ++p) {
            const size_t a = kGfxPlanes + size_t(p) * plane_size + off + size_t(y) * size_t(wbytes) + size_t(x / 8);
            if (a >= im.size()) throw Error("graphics data past end of image");
            v |= ((im[a] >> (7 - x % 8)) & 1) << p;
        }
        return uint8_t(v);
    };
    d.tile8.count = tile8_count;
    d.tile8.pixels.resize(size_t(tile8_count) * 64);
    for (int t = 0; t < tile8_count; ++t)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) d.tile8.pixels[size_t(t) * 64 + size_t(y) * 8 + size_t(x)] = pixel(uint32_t(t) * 8, x, y, 1);
    for (int i = 0; i < pic_count; ++i) {
        const size_t r = pic_table + size_t(i) * 16;
        NamedPicture p;
        const int wb = rd16(im, r), h = rd16(im, r + 2);
        const uint32_t off = rd32(im, r + 4) + pic_base;
        for (size_t k = 8; k < 16 && im[r + k]; ++k) p.name.push_back(char(im[r + k]));
        p.width = wb * 8;
        p.height = h;
        p.pixels.resize(size_t(p.width) * size_t(h));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < p.width; ++x) p.pixels[size_t(y) * size_t(p.width) + size_t(x)] = pixel(off, x, y, wb);
        d.pics.push_back(std::move(p));
    }

    // --- font: u16 height, u16 location[256], u8 width[256]; each glyph = outline bitmap then glyph bitmap ---
    d.font.height = rd16(im, kFontBase);
    for (int c = 0; c < 256; ++c) {
        const int w = im.at(kFontBase + 0x202 + size_t(c));
        d.font.width[size_t(c)] = uint8_t(w);
        if (!w) continue;
        const int bpr = (w + 7) / 8;
        const size_t glyph = kFontBase + rd16(im, kFontBase + 2 + size_t(c) * 2) + size_t(bpr * d.font.height);
        auto& rows = d.font.rows[size_t(c)];
        rows.resize(size_t(w * d.font.height));
        for (int y = 0; y < d.font.height; ++y)
            for (int x = 0; x < w; ++x)
                rows[size_t(y * w + x)] = (im.at(glyph + size_t(y * bpr + x / 8)) >> (7 - x % 8)) & 1;
    }

    // --- sounds: "SND\0", u16 data size, u16 count; then 16-byte records {u16 offset, u8 priority, u8 ?, name[12]} ---
    if (std::memcmp(&im[kSoundBase], "SND", 4) != 0) throw Error("sound table not found");
    const int sound_count = rd16(im, kSoundBase + 6);
    d.sounds.resize(size_t(sound_count) + 1);
    for (int n = 1; n <= sound_count; ++n) {
        const size_t r = kSoundBase + size_t(n) * 16;
        Sound& s = d.sounds[size_t(n)];
        s.priority = im[r + 2];
        for (size_t k = 4; k < 16 && im[r + k]; ++k) s.name.push_back(char(im[r + k]));
        for (size_t o = kSoundBase + rd16(im, r);; o += 2) {
            const uint16_t v = rd16(im, o);
            if (v == 0xFFFF) break;
            s.divisors.push_back(v);
            if (s.divisors.size() > 20000) throw Error("unterminated sound");
        }
    }
    return d;
}

}  // namespace dd2
