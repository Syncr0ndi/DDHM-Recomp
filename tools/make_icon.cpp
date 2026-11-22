// SPDX-License-Identifier: MIT
// make_icon - writes the port's Windows icon (ddhm.ico, 16/32/48 px) from a small original pixel design below.
// The design is this project's own (a moonlit haunted house), not artwork from the game.
// usage: make_icon <out.ico>
#include <cstdint>
#include <cstdio>
#include <vector>

static const char* const kArt[16] = {
    "..BBBBBBBBBBBB..",
    ".BBBBBBBBBYYBBB.",
    "BBBBBBBBBYYYYBBB",
    "BBBBBBBBBYYYYBBB",
    "BBBBKBBBBBYYBBBB",
    "BBBKKKBBBBBBBBBB",
    "BBKKKKKBBBBBBBBB",
    "BKKKKKKKBBBKBBBB",
    "BBKyKyKBBBKKKBBB",
    "BBKKKKKBBKKKKKBB",
    "BBKyKyKBBKyKyKBB",
    "BBKKKKKKKKKKKKBB",
    "BBKyKKyKKKyKKKBB",
    "BBKKKdKKKKKdKKBB",
    ".GGGGGGGGGGGGGG.",
    "..GGGGGGGGGGGG..",
};

static uint32_t color(char c) {   // 0xAARRGGBB
    switch (c) {
        case 'B': return 0xFF101848;   // night sky
        case 'Y': return 0xFFF8F070;   // moon
        case 'K': return 0xFF101010;   // house
        case 'y': return 0xFFF8D040;   // lit window
        case 'd': return 0xFF884010;   // door
        case 'G': return 0xFF206020;   // grass
        default: return 0;             // transparent
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: make_icon <out.ico>\n"); return 2; }
    const int sizes[3] = {16, 32, 48};
    std::vector<std::vector<uint8_t>> images;
    for (int s : sizes) {
        const int scale = s / 16;
        std::vector<uint8_t> img;
        auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) img.push_back(uint8_t(v >> (8 * i))); };
        auto u16 = [&](uint16_t v) { img.push_back(uint8_t(v)); img.push_back(uint8_t(v >> 8)); };
        u32(40); u32(uint32_t(s)); u32(uint32_t(s * 2)); u16(1); u16(32); u32(0); u32(0); u32(0); u32(0); u32(0); u32(0);
        for (int y = s - 1; y >= 0; --y)                       // XOR image, bottom-up BGRA
            for (int x = 0; x < s; ++x) {
                const uint32_t c = color(kArt[y / scale][x / scale]);
                img.push_back(uint8_t(c)); img.push_back(uint8_t(c >> 8)); img.push_back(uint8_t(c >> 16)); img.push_back(uint8_t(c >> 24));
            }
        const int mask_stride = ((s + 31) / 32) * 4;           // AND mask, 1 = transparent
        for (int y = s - 1; y >= 0; --y) {
            std::vector<uint8_t> row(size_t(mask_stride), 0);
            for (int x = 0; x < s; ++x)
                if (!color(kArt[y / scale][x / scale])) row[size_t(x / 8)] |= uint8_t(0x80 >> (x % 8));
            img.insert(img.end(), row.begin(), row.end());
        }
        images.push_back(std::move(img));
    }
    FILE* f = std::fopen(argv[1], "wb");
    if (!f) { std::perror(argv[1]); return 1; }
    auto w16 = [&](uint16_t v) { std::fputc(v & 0xFF, f); std::fputc(v >> 8, f); };
    auto w32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) std::fputc(int((v >> (8 * i)) & 0xFF), f); };
    w16(0); w16(1); w16(3);
    uint32_t offset = 6 + 16 * 3;
    for (size_t i = 0; i < 3; ++i) {
        std::fputc(sizes[i], f); std::fputc(sizes[i], f); std::fputc(0, f); std::fputc(0, f);
        w16(1); w16(32); w32(uint32_t(images[i].size())); w32(offset);
        offset += uint32_t(images[i].size());
    }
    for (const auto& img : images) std::fwrite(img.data(), 1, img.size(), f);
    std::fclose(f);
    return 0;
}
