// SPDX-License-Identifier: MIT
// screen.hpp - the EGA display as the original draws to it, kept as palette indices.
//
// The DOS game draws into a video-memory page wider than the screen and shows a 320x200 window of it, moved with the
// CRTC start address and the pixel-panning register. Window and text routines address that page, not the visible
// screen: during play the page origin sits on the tile-aligned camera position and the visible window starts a few
// pixels further in, so a message box shifts with the sub-tile scroll exactly as in the original. Canvas = that page,
// view_x / view_y = the start address and pel pan.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "dd2/dd2.hpp"
#include "dd2/exe.hpp"

namespace ddhm {

class Screen {
public:
    static constexpr int kW = 320, kH = 200;          // visible area
    static constexpr int kCanvasW = 640, kCanvasH = 240;

    explicit Screen(const dd2::ExeData* exe = nullptr) : exe_(exe) { clear(); }
    void set_exe(const dd2::ExeData* e) { exe_ = e; }

    std::array<uint8_t, size_t(kCanvasW) * kCanvasH> canvas{};
    int view_x = 0, view_y = 0;                       // top-left of the visible window inside the canvas
    int org_x = 0, org_y = 0;                         // where the drawing routines' origin (DS:0x3172) sits in the canvas
    std::array<uint8_t, 16> palette{0, 1, 2, 3, 4, 5, 6, 7, 24, 25, 26, 27, 28, 29, 30, 31};   // EGA palette registers

    // Text state of the original (DS:0x439C cursor x, 0x439E cursor y, 0x98EE / 0x98F0 window text margins,
    // 0x439A colour). Pixel units.
    int cur_x = 0, cur_y = 0, margin_l = 0, margin_r = kW, color = 15;

    void clear(uint8_t c = 0);
    uint8_t& at(int x, int y) { return canvas[size_t(y) * kCanvasW + size_t(x)]; }
    void put(int x, int y, uint8_t c) { if (x >= 0 && y >= 0 && x < kCanvasW && y < kCanvasH) at(x, y) = c; }
    void put_o(int x, int y, uint8_t c) { put(x + org_x, y + org_y, c); }   // relative to the drawing origin
    void fill(int x, int y, int w, int h, uint8_t c);
    void visible(std::vector<uint8_t>& out) const;    // the 320x200 window, palette indices
    std::vector<uint8_t> rgba() const;                // the 320x200 window as RGBA, through the palette registers
    static dd2::Rgb ega_rgb(uint8_t v);               // palette register value -> colour (200-line modes: RGBI)

    // Primitives of the original.
    void tile8(int col, int y, int t);                // 0x9450: 8x8 tile at byte column col, pixel row y
    void window(int x0, int y0, int x1, int y1);      // 0x82BE: framed box over 8x8 cells, sets the text margins
    void center_window(int w, int h);                 // 0x8424: box of w x h inner cells centred on (19,11)
    void glyph(uint8_t c);                            // 0x963D: XOR one character in `color`, advance cur_x
    void print(const std::string& s);                 // 0x8A61: '\n' = 10 px down to the left margin, 0x7F = colour
    void print_centered(const std::string& s);        // 0x8B46: centre between the margins, then print
    void picture(const dd2::Picture& p, int x, int y);   // 0x94EE: opaque picture at pixel x (byte aligned), y
    void sprite(const dd2::Sprite& s, int x, int y, bool white = false);   // masked sprite; white = hit flash (0x64BF)
    void tile16(const dd2::TileSet& ts, int t, int x, int y);

    int center_col = 0x13, center_row = 0x0B;          // DS:0x2A30 / 0x2A32 (main sets 0x13, 0x0B)

private:
    const dd2::ExeData* exe_ = nullptr;
};

}  // namespace ddhm
