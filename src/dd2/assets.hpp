// SPDX-License-Identifier: MIT
// assets.hpp - the "Game" -> "Assets" step. The player extracts their own copy of the MS-DOS game into a folder named
// Game; on first launch the files the port needs are checked and written to a folder named Assets:
//   DAVEDATA.BIN              DAVE.EXE unpacked (LZEXE) - the data inside it (font, pictures, sounds, texts, tables)
//   EGATILES.DD2, S_*.DD2, LEVEL01..08.DD2, TITLE1.DD2, TITLE2.DD2, PROGPIC.DD2   copied unchanged
//   MANIFEST.TXT              written last: format version, source version and size + CRC-32 of every file above
// The port itself contains none of these; without them it does not start.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace dd2 {

extern const char* const kRequiredDataFiles[];        // the .DD2 files copied to Assets
extern const int kRequiredDataFileCount;

struct FolderCheck {
    bool ok = false;
    std::string message;                              // what is wrong, for the player
};

// Is this a complete copy of the supported game (Softdisk DAVE.EXE 1.12 plus every required .DD2, all decodable)?
FolderCheck check_game_folder(const std::string& game_dir);

// Is the Assets folder complete and unchanged since it was written (manifest present, sizes and CRCs match)?
FolderCheck check_assets(const std::string& assets_dir);

// Checks the game folder, then writes the Assets folder (replacing an old one). Throws dd2::Error on failure.
// `progress` receives short status lines.
void extract_assets(const std::string& game_dir, const std::string& assets_dir,
                    const std::function<void(const std::string&)>& progress = {});

}  // namespace dd2
