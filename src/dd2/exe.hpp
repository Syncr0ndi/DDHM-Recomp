// SPDX-License-Identifier: MIT
// exe.hpp - data that lives inside DAVE.EXE rather than in the .DD2 files: the window tiles, the proportional
// text font, the full-screen death pictures, the PC-speaker sound effects and the game's text strings.
//
// The executable is LZEXE 0.91 packed; it is unpacked in memory every time the port starts, from the user's own copy.
// Nothing extracted from it is ever stored in this repository. All offsets below are for the Softdisk release
// (DAVE.EXE 84,222 bytes, "Version 1.12 (rev 1)"), which load_exe() checks.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "dd2/dd2.hpp"

namespace dd2 {

// Unpacks an LZEXE 0.91 executable and returns its load image (the bytes after the MZ header, before relocation).
Bytes lzexe_unpack(const Bytes& exe);

constexpr uint32_t kDataSegment = 0x27130;   // DGROUP (DS) as an image offset

// 8x8 tiles used for window borders (drawn by 0x9450). The EXE has 32 of them.
struct Tile8Set {
    int count = 0;
    std::vector<uint8_t> pixels;                 // count * 64 palette indices
    const uint8_t* tile(int i) const { return &pixels[size_t(i) * 64]; }
};

// Full-screen pictures drawn by 0x94EE: 8 death animations of 5 frames (48x48) and "THE END" (208x56).
struct NamedPicture : Picture { std::string name; };

// The proportional font used for every message (fontstruct at image 0x106D0, glyph draw 0x963D).
struct Font {
    int height = 0;
    std::array<uint8_t, 256> width{};
    std::array<std::vector<uint8_t>, 256> rows;  // per glyph: height * width bits, one byte per pixel (0/1)
    int text_width(const std::string& s) const;  // 0x8B0E (skips the 0x7F colour escape and its argument)
};

// PC-speaker effects: one PIT channel-2 divisor per 140 Hz timer tick, 0 = silent tick (table at image 0x257C0,
// player in the timer interrupt 0x904B). A new sound replaces the current one when its priority is >= (0x8FA8).
struct Sound {
    std::string name;
    uint8_t priority = 0;
    std::vector<uint16_t> divisors;
};

struct ExeData {
    Bytes image;                         // unpacked load image
    Tile8Set tile8;
    std::vector<NamedPicture> pics;      // 41 pictures
    Font font;
    std::vector<Sound> sounds;           // index = sound number used by the game (0 unused)

    std::string ds_string(uint16_t ds_offset) const;   // NUL-terminated string in DGROUP
    uint16_t ds_word(uint16_t ds_offset) const;
    uint8_t ds_byte(uint16_t ds_offset) const;
};

// Loads and decodes DAVE.EXE. Throws dd2::Error if the file is missing or is not the supported version.
// `path` may also be DAVEDATA.BIN (the unpacked image extract_assets() writes). Installs the game tables.
ExeData load_exe(const std::string& path);
// Loads DAVEDATA.BIN or DAVE.EXE from a folder (an Assets or a game folder) and returns it; throws if neither works.
ExeData load_exe_from_dir(const std::string& dir);

}  // namespace dd2
