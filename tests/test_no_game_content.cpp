// SPDX-License-Identifier: MIT
// Copyright check of the shipped executables: no part of the original game may be inside them.
// Searches every non-trivial 32-byte block of every game file (and of DAVE.EXE unpacked) in the given executables,
// and every text string (6+ characters) of DAVE.EXE's data segment.
// Allowed: the data file names the port has to look for, counting sequences (e.g. 4, 8, 12 ... or "GHIJ..."), and
// C runtime words that the 1991 compiler library shares with today's ("Overflow", "(null)").
// usage: test_no_game_content <game-dir> <exe> [<exe> ...]
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "dd2/dd2.hpp"
#include "dd2/exe.hpp"

namespace fs = std::filesystem;

namespace {
bool contains(const dd2::Bytes& hay, const uint8_t* p, size_t n) {
    if (hay.size() < n) return false;
    for (size_t i = 0; i + n <= hay.size(); ++i) {
        if (hay[i] != p[0]) continue;
        size_t k = 1;
        while (k < n && hay[i + k] == p[k]) ++k;
        if (k == n) return true;
    }
    return false;
}
bool trivial(const uint8_t* p) {
    int distinct = 0;
    bool seen[256] = {};
    for (int i = 0; i < 32; ++i) if (!seen[p[i]]) { seen[p[i]] = true; ++distinct; }
    if (distinct < 6) return true;
    bool names = true;                                   // file-name lists: A-Z 0-9 _ . and NUL only
    for (int i = 0; i < 32; ++i) {
        const uint8_t c = p[i];
        if (!(c == 0 || c == '_' || c == '.' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) { names = false; break; }
    }
    if (names) return true;
    for (int off = 0; off < 2; ++off) {                  // arithmetic sequences of 16-bit values
        bool seq = true;
        const int a = p[off] | (p[off + 1] << 8), b = p[off + 2] | (p[off + 3] << 8);
        for (int i = off + 4; i + 1 < 32; i += 2)
            if ((p[i] | (p[i + 1] << 8)) - (p[i - 2] | (p[i - 1] << 8)) != b - a) { seq = false; break; }
        if (seq) return true;
    }
    return false;
}
bool allowed_string(const std::string& s) {
    if (s.find(".DD2") != std::string::npos || s.find(".EXE") != std::string::npos || s == "LEVEL0") return true;
    static const char* const kRuntime[] = {"(null)", "0123456789ABCDEF", "Denormal", "OVERFLOW", "Overflow", "UNDERFLOW",
                                           "Underflow", "Divide by zero", "Stack overflow"};
    for (const char* r : kRuntime) if (s == r) return true;
    return false;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: test_no_game_content <game-dir> <exe>...\n"); return 2; }
    std::vector<std::pair<std::string, dd2::Bytes>> exes;
    for (int i = 2; i < argc; ++i) exes.push_back({argv[i], dd2::read_file(argv[i])});
    std::vector<std::pair<std::string, dd2::Bytes>> sources;
    for (const auto& e : fs::directory_iterator(dd2::utf8_path(argv[1]))) {
        std::string n = dd2::path_utf8(e.path().filename());
        for (char& c : n) c = char((c >= 'a' && c <= 'z') ? c - 32 : c);
        if (n.size() > 4 && (n.substr(n.size() - 4) == ".DD2" || n.substr(n.size() - 4) == ".EXE"))
            sources.push_back({n, dd2::read_file(dd2::path_utf8(e.path()))});
    }
    const dd2::ExeData dave = dd2::load_exe_from_dir(argv[1]);
    sources.push_back({"DAVE.EXE (unpacked)", dave.image});
    int fail = 0;
    size_t checked = 0;
    for (const auto& [name, data] : sources)
        for (size_t i = 0; i + 32 <= data.size(); i += 32) {
            if (trivial(&data[i])) continue;
            ++checked;
            for (const auto& [en, eb] : exes)
                if (contains(eb, &data[i], 32)) {
                    if (++fail <= 20) std::printf("FAIL %s bytes at 0x%zx are inside %s\n", name.c_str(), i, en.c_str());
                }
        }
    size_t strings = 0;
    const dd2::Bytes& im = dave.image;
    for (size_t i = dd2::kDataSegment; i < im.size();) {
        size_t j = i;
        while (j < im.size() && im[j] >= 0x20 && im[j] < 0x7F) ++j;
        if (j - i >= 6) {
            const std::string s(im.begin() + std::ptrdiff_t(i), im.begin() + std::ptrdiff_t(j));
            ++strings;
            if (!allowed_string(s))
                for (const auto& [en, eb] : exes)
                    if (contains(eb, reinterpret_cast<const uint8_t*>(s.data()), s.size())) {
                        std::printf("FAIL string \"%s\" is inside %s\n", s.c_str(), en.c_str());
                        ++fail;
                    }
        }
        i = j + 1;
    }
    std::printf("%zu blocks and %zu strings of the game checked against %zu executable(s): %s\n", checked, strings,
                exes.size(), fail ? "GAME CONTENT FOUND" : "none found");
    return fail ? 1 : 0;
}
