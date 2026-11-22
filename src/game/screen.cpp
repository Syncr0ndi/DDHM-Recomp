// SPDX-License-Identifier: MIT
#include "game/screen.hpp"

#include <algorithm>

namespace ddhm {

void Screen::clear(uint8_t c) { canvas.fill(c); }

void Screen::fill(int x, int y, int w, int h, uint8_t c) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) put(x + i, y + j, c);
}

void Screen::visible(std::vector<uint8_t>& out) const {
    out.resize(size_t(kW) * kH);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const int cx = view_x + x, cy = view_y + y;
            out[size_t(y) * kW + size_t(x)] =
                (cx >= 0 && cy >= 0 && cx < kCanvasW && cy < kCanvasH) ? canvas[size_t(cy) * kCanvasW + size_t(cx)] : 0;
        }
}

// In the 200-line EGA modes the monitor is driven as RGBI: palette bits 0-2 are blue/green/red and bit 4 is intensity
// (bits 3 and 5 are ignored). Value 6 shows as brown, as on every CGA/EGA monitor.
dd2::Rgb Screen::ega_rgb(uint8_t v) {
    const int i = (v >> 4) & 1, b = v & 1, g = (v >> 1) & 1, r = (v >> 2) & 1;
    const int idx = b | (g << 1) | (r << 2) | (i << 3);
    return dd2::kEgaPalette[size_t(idx)];
}

std::vector<uint8_t> Screen::rgba() const {
    std::vector<uint8_t> idx, out(size_t(kW) * kH * 4);
    visible(idx);
    for (size_t i = 0; i < idx.size(); ++i) {
        const dd2::Rgb c = ega_rgb(palette[idx[i] & 15]);
        out[i * 4] = c.r; out[i * 4 + 1] = c.g; out[i * 4 + 2] = c.b; out[i * 4 + 3] = 255;
    }
    return out;
}

void Screen::tile8(int col, int y, int t) {
    if (!exe_ || t < 0 || t >= exe_->tile8.count) return;
    const uint8_t* p = exe_->tile8.tile(t);
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i) put_o(col * 8 + i, y + j, p[j * 8 + i]);
}

// 0x82BE: corners 1/3/6/8, top 2, bottom 7, sides 4/5, inside 0x1B.
void Screen::window(int x0, int y0, int x1, int y1) {
    margin_l = x0 * 8 + 8;
    margin_r = x1 * 8;
    tile8(x0, y0 * 8, 1);
    for (int x = x0 + 1; x < x1; ++x) tile8(x, y0 * 8, 2);
    tile8(x1, y0 * 8, 3);
    for (int y = y0 + 1; y < y1; ++y) {
        tile8(x0, y * 8, 4);
        for (int x = x0 + 1; x < x1; ++x) tile8(x, y * 8, 0x1B);
        tile8(x1, y * 8, 5);
    }
    tile8(x0, y1 * 8, 6);
    for (int x = x0 + 1; x < x1; ++x) tile8(x, y1 * 8, 7);
    tile8(x1, y1 * 8, 8);
    cur_x = (x0 + 1) * 8;
    cur_y = (y0 + 1) * 8;
}

void Screen::center_window(int w, int h) {
    const int x0 = center_col - w / 2, y0 = center_row - h / 2;
    window(x0, y0, x0 + w + 1, y0 + h + 1);
}

// 0x963D runs in EGA write mode 0 with the XOR function (DS:0x43A0 = 0x18) and the map mask set to the text colour:
// every set glyph bit flips the colour's planes of that pixel.
void Screen::glyph(uint8_t c) {
    if (!exe_) return;
    const dd2::Font& f = exe_->font;
    const int w = f.width[c];
    const auto& rows = f.rows[c];
    for (int y = 0; y < f.height && !rows.empty(); ++y)
        for (int x = 0; x < w; ++x)
            if (rows[size_t(y * w + x)]) {
                const int px = cur_x + x + org_x, py = cur_y + y + org_y;
                if (px >= 0 && py >= 0 && px < kCanvasW && py < kCanvasH) at(px, py) ^= uint8_t(color & 15);
            }
    cur_x += w;
}

void Screen::print(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        const uint8_t c = uint8_t(s[i]);
        if (c == '\n') { cur_y += 10; cur_x = margin_l; }
        else if (c == 0x7F) { if (i + 1 < s.size()) color = uint8_t(s[++i]) - 'A'; }
        else glyph(c);
    }
}

void Screen::print_centered(const std::string& s) {
    const int w = exe_ ? exe_->font.text_width(s) : 0;
    cur_x = margin_l + (margin_r - margin_l - w) / 2;      // idiv: truncates toward zero
    print(s);
}

void Screen::picture(const dd2::Picture& p, int x, int y) {
    for (int j = 0; j < p.height; ++j)
        for (int i = 0; i < p.width; ++i) put_o(x + i, y + j, p.pixels[size_t(j) * size_t(p.width) + size_t(i)]);
}

void Screen::sprite(const dd2::Sprite& s, int x, int y, bool white) {
    for (int j = 0; j < s.height; ++j)
        for (int i = 0; i < s.width; ++i) {
            const uint8_t c = s.pixels[size_t(j) * size_t(s.width) + size_t(i)];
            if (c != dd2::Sprite::kTransparent) put_o(x + i, y + j, white ? 15 : c);
        }
}

void Screen::tile16(const dd2::TileSet& ts, int t, int x, int y) {
    if (t < 0 || t >= ts.count) return;
    const uint8_t* p = ts.tile(t);
    for (int j = 0; j < 16; ++j)
        for (int i = 0; i < 16; ++i) put_o(x + i, y + j, p[j * 16 + i]);
}

}  // namespace ddhm
