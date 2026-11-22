// SPDX-License-Identifier: MIT
#include "dd2/assets.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "dd2/dd2.hpp"
#include "dd2/exe.hpp"

namespace fs = std::filesystem;

namespace dd2 {

const char* const kRequiredDataFiles[] = {
    "EGATILES.DD2", "S_DAVE.DD2", "S_CHUNK1.DD2", "S_CHUNK2.DD2", "S_FRANK.DD2", "S_MASTER.DD2",
    "LEVEL01.DD2", "LEVEL02.DD2", "LEVEL03.DD2", "LEVEL04.DD2", "LEVEL05.DD2", "LEVEL06.DD2", "LEVEL07.DD2",
    "LEVEL08.DD2", "TITLE1.DD2", "TITLE2.DD2", "PROGPIC.DD2"};
const int kRequiredDataFileCount = int(sizeof kRequiredDataFiles / sizeof kRequiredDataFiles[0]);

namespace {
constexpr const char* kManifestHeader = "DDHM-Recomp assets 1";
constexpr const char* kSource = "DAVE.EXE 1.12";

std::string slash(std::string d) {
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d;
}

uint32_t crc32(const Bytes& d) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : d) {
        c ^= b;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

bool exists(const std::string& p) {
    std::error_code ec;
    return fs::is_regular_file(utf8_path(p), ec);
}

// The game folder may hold the files in any letter case (DOS names are case-insensitive).
std::string find_file(const std::string& dir, const std::string& name) {
    if (exists(dir + name)) return dir + name;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(utf8_path(dir), ec)) {
        std::string n = path_utf8(e.path().filename());
        std::string u = n;
        for (char& ch : u) ch = char((ch >= 'a' && ch <= 'z') ? ch - 32 : ch);
        if (u == name) return dir + n;
    }
    return {};
}

void write_file(const std::string& path, const Bytes& b) {
    std::ofstream f(utf8_path(path), std::ios::binary | std::ios::trunc);
    if (!f || !f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size())))
        throw Error("cannot write " + path);
}
}  // namespace

FolderCheck check_game_folder(const std::string& game_dir) {
    FolderCheck r;
    const std::string dir = slash(game_dir);
    std::error_code ec;
    if (game_dir.empty() || !fs::is_directory(utf8_path(game_dir), ec)) {
        r.message = "The Game folder does not exist yet.";
        return r;
    }
    std::string missing;
    int n_missing = 0;
    const std::string exe = find_file(dir, "DAVE.EXE");
    if (exe.empty()) { missing += " DAVE.EXE"; ++n_missing; }
    for (int i = 0; i < kRequiredDataFileCount; ++i)
        if (find_file(dir, kRequiredDataFiles[i]).empty()) { missing += std::string(" ") + kRequiredDataFiles[i]; ++n_missing; }
    if (n_missing == kRequiredDataFileCount + 1) {
        r.message = "No game files in the Game folder yet.";
        return r;
    }
    if (n_missing > 0) {
        r.message = "Files missing from the Game folder:" + missing;
        return r;
    }
    try {
        load_exe(exe);                                  // unpacks and checks the version, installs the tables
        load_tileset(find_file(dir, "EGATILES.DD2"));
        for (int i = 1; i <= 8; ++i) {
            char n[16];
            std::snprintf(n, sizeof n, "LEVEL%02d.DD2", i);
            load_level(find_file(dir, n));
        }
        for (const char* p : {"TITLE1.DD2", "TITLE2.DD2", "PROGPIC.DD2"}) load_picture(find_file(dir, p));
        // load_sprites wants the five files under their usual names in one folder
        for (const char* s : {"S_DAVE.DD2", "S_CHUNK1.DD2", "S_CHUNK2.DD2", "S_FRANK.DD2", "S_MASTER.DD2"})
            if (find_file(dir, s) != dir + s) throw Error(std::string("please name the file ") + s + " in capitals");
        load_sprites(dir);
    } catch (const std::exception& e) {
        r.message = std::string("The game files are not usable: ") + e.what();
        return r;
    }
    r.ok = true;
    r.message = "Dangerous Dave in the Haunted Mansion (Softdisk, version 1.12) found.";
    return r;
}

FolderCheck check_assets(const std::string& assets_dir) {
    FolderCheck r;
    const std::string dir = slash(assets_dir);
    std::ifstream f(utf8_path(dir + "MANIFEST.TXT"));
    std::string line;
    if (!f || !std::getline(f, line) || line.rfind(kManifestHeader, 0) != 0) {
        r.message = "Assets have not been extracted yet.";
        return r;
    }
    int count = 0;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.rfind("source ", 0) == 0) continue;
        std::istringstream ls(line);
        std::string name;
        size_t size = 0;
        uint32_t crc = 0;
        if (!(ls >> name >> size >> std::hex >> crc)) continue;
        try {
            const Bytes b = read_file(dir + name);
            if (b.size() != size || crc32(b) != crc) { r.message = "Asset " + name + " has changed; extract again."; return r; }
        } catch (const std::exception&) {
            r.message = "Asset " + name + " is missing; extract again.";
            return r;
        }
        ++count;
    }
    if (count != kRequiredDataFileCount + 1) { r.message = "Assets are incomplete; extract again."; return r; }
    r.ok = true;
    r.message = "Assets ready.";
    return r;
}

void extract_assets(const std::string& game_dir, const std::string& assets_dir,
                    const std::function<void(const std::string&)>& progress) {
    auto say = [&](const std::string& s) { if (progress) progress(s); };
    say("Checking the game files...");
    const FolderCheck g = check_game_folder(game_dir);
    if (!g.ok) throw Error(g.message);
    const std::string in = slash(game_dir), out = slash(assets_dir);
    std::error_code ec;
    fs::create_directories(utf8_path(assets_dir), ec);
    if (ec) throw Error("cannot create the Assets folder: " + ec.message());
    fs::remove(utf8_path(out + "MANIFEST.TXT"), ec);   // incomplete until the manifest is written again

    std::ostringstream manifest;
    manifest << kManifestHeader << "\nsource " << kSource << "\n";
    auto add = [&](const std::string& name, const Bytes& b) {
        write_file(out + name, b);
        char line[96];
        std::snprintf(line, sizeof line, "%s %zu %08x\n", name.c_str(), b.size(), crc32(b));
        manifest << line;
    };
    say("Unpacking DAVE.EXE...");
    add("DAVEDATA.BIN", lzexe_unpack(read_file(find_file(in, "DAVE.EXE"))));
    for (int i = 0; i < kRequiredDataFileCount; ++i) {
        say(std::string("Copying ") + kRequiredDataFiles[i] + "...");
        add(kRequiredDataFiles[i], read_file(find_file(in, kRequiredDataFiles[i])));
    }
    std::ofstream m(utf8_path(out + "MANIFEST.TXT"), std::ios::trunc);
    m << manifest.str();
    if (!m) throw Error("cannot write MANIFEST.TXT");
    m.close();
    const FolderCheck a = check_assets(assets_dir);
    if (!a.ok) throw Error(a.message);
    say("Assets extracted.");
}

}  // namespace dd2
