// SPDX-License-Identifier: MIT
// DDHM Launcher - checks the player's copy of the MS-DOS game in the "Game" folder, extracts the files the port needs
// into "Assets" (first launch, or on request), keeps the display / sound options and starts ddhm.exe.
// Native Win32 so it runs before any game data exists.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "dd2/assets.hpp"
#include "dd2/dd2.hpp"

#ifdef _MSC_VER
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace fs = std::filesystem;

namespace {

enum : int { kPlay = 101, kExtract, kOpenGame, kQuit, kFullscreen, kSquare, kSoft };

HWND g_status, g_play, g_full, g_square, g_soft;
HFONT g_font, g_title_font;
fs::path g_base, g_game, g_assets, g_ini;
std::map<std::string, std::string> g_settings;     // ddhm.ini, shared with ddhm.exe
const wchar_t* const kTitle = L"Dangerous Dave in the Haunted Mansion";

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

void load_settings() {
    std::ifstream f(g_ini);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq != std::string::npos) g_settings[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

void save_settings() {
    g_settings["fullscreen"] = SendMessageW(g_full, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0";
    g_settings["square_pixels"] = SendMessageW(g_square, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0";
    g_settings["soft_sound"] = SendMessageW(g_soft, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0";
    std::error_code ec;
    fs::create_directories(g_ini.parent_path(), ec);
    std::ofstream f(g_ini, std::ios::trunc);
    for (const auto& [k, v] : g_settings) f << k << "=" << v << "\n";
}

void set_status(const std::wstring& s) {
    SetWindowTextW(g_status, s.c_str());
    UpdateWindow(g_status);
}

// Shows what is there; returns true when the game can be started (assets ready, or extractable).
bool refresh() {
    const dd2::FolderCheck a = dd2::check_assets(dd2::path_utf8(g_assets));
    if (a.ok) {
        set_status(L"Game files: extracted.\r\nAssets: ready.\r\n\r\nPress Play.");
        return true;
    }
    const dd2::FolderCheck g = dd2::check_game_folder(dd2::path_utf8(g_game));
    if (g.ok) {
        set_status(L"Game files: " + wide(g.message) + L"\r\nAssets: not extracted yet. Play extracts them first.");
        return true;
    }
    set_status(L"This port needs your own copy of the MS-DOS game.\r\n" + wide(g.message) +
               L"\r\n\r\nExtract the original game (DAVE.EXE, EGATILES.DD2, LEVEL01.DD2 ...) into the Game folder,"
               L" then press Play.\r\n" + g_game.wstring());
    return false;
}

bool extract() {
    try {
        dd2::extract_assets(dd2::path_utf8(g_game), dd2::path_utf8(g_assets),
                            [](const std::string& s) { set_status(wide(s)); });
        return true;
    } catch (const std::exception& e) {
        MessageBoxW(nullptr, (L"The assets could not be extracted.\n\n" + wide(e.what())).c_str(), kTitle, MB_ICONERROR);
        refresh();
        return false;
    }
}

void open_game_folder() {
    std::error_code ec;
    fs::create_directories(g_game, ec);
    ShellExecuteW(nullptr, L"open", g_game.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool play(HWND wnd) {
    if (!dd2::check_assets(dd2::path_utf8(g_assets)).ok) {
        if (!dd2::check_game_folder(dd2::path_utf8(g_game)).ok) {
            refresh();
            if (MessageBoxW(wnd, L"The original game files were not found in the Game folder.\n\nOpen the Game folder now?",
                            kTitle, MB_ICONINFORMATION | MB_YESNO) == IDYES)
                open_game_folder();
            return false;
        }
        if (!extract()) return false;
    }
    save_settings();
    const fs::path exe = g_base / "ddhm.exe";
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, g_base.c_str(), &si, &pi)) {
        MessageBoxW(wnd, (L"Cannot start " + exe.wstring()).c_str(), kTitle, MB_ICONERROR);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

LRESULT CALLBACK wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case kPlay: if (play(w)) DestroyWindow(w); return 0;
                case kExtract:
                    if (dd2::check_game_folder(dd2::path_utf8(g_game)).ok) { if (extract()) refresh(); }
                    else refresh();
                    return 0;
                case kOpenGame: open_game_folder(); return 0;
                case kQuit: DestroyWindow(w); return 0;
                case kFullscreen: case kSquare: case kSoft: save_settings(); return 0;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE) refresh();      // the player may have copied files meanwhile
            break;
        case WM_CTLCOLORSTATIC:
            SetBkMode(HDC(wp), TRANSPARENT);
            return LRESULT(GetSysColorBrush(COLOR_WINDOW));
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    SetProcessDPIAware();
    wchar_t buf[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    g_base = fs::path(buf).parent_path();
    g_game = g_base / L"Game";
    g_assets = g_base / L"Assets";
    PWSTR appdata = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        g_ini = fs::path(appdata) / L"DDHM-Recomp" / L"DDHM" / L"ddhm.ini";   // same place as SDL_GetPrefPath
        CoTaskMemFree(appdata);
    }
    load_settings();

    HDC screen = GetDC(nullptr);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW big = ncm.lfMessageFont;
    big.lfHeight = -S(18);
    big.lfWeight = FW_BOLD;
    g_title_font = CreateFontIndirectW(&big);

    WNDCLASSW wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = L"DDHMLauncher";
    RegisterClassW(&wc);

    RECT r{0, 0, S(520), S(330)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    HWND w = CreateWindowW(L"DDHMLauncher", L"DDHM Launcher", style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                           r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    auto ctl = [&](const wchar_t* cls, const wchar_t* text, DWORD st, int x, int y, int cw, int ch, int id) {
        HWND h = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | st, S(x), S(y), S(cw), S(ch), w,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
        SendMessageW(h, WM_SETFONT, WPARAM(id == -2 ? g_title_font : g_font), TRUE);
        return h;
    };
    ctl(L"STATIC", kTitle, 0, 20, 16, 480, 30, -2);
    ctl(L"STATIC", L"Native Windows port. Uses your own copy of the original MS-DOS game.", 0, 20, 48, 480, 20, -1);
    g_status = ctl(L"STATIC", L"", SS_LEFT, 20, 80, 480, 110, -1);
    g_full = ctl(L"BUTTON", L"Fullscreen", BS_AUTOCHECKBOX | WS_TABSTOP, 20, 200, 150, 22, kFullscreen);
    g_square = ctl(L"BUTTON", L"Square pixels (no 4:3)", BS_AUTOCHECKBOX | WS_TABSTOP, 180, 200, 180, 22, kSquare);
    g_soft = ctl(L"BUTTON", L"Softer sound", BS_AUTOCHECKBOX | WS_TABSTOP, 370, 200, 130, 22, kSoft);
    g_play = ctl(L"BUTTON", L"Play", BS_DEFPUSHBUTTON | WS_TABSTOP, 20, 270, 110, 34, kPlay);
    ctl(L"BUTTON", L"Extract assets again", WS_TABSTOP, 140, 270, 150, 34, kExtract);
    ctl(L"BUTTON", L"Open Game folder", WS_TABSTOP, 300, 270, 120, 34, kOpenGame);
    ctl(L"BUTTON", L"Quit", WS_TABSTOP, 430, 270, 70, 34, kQuit);
    SendMessageW(g_full, BM_SETCHECK, g_settings["fullscreen"] == "1" ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_square, BM_SETCHECK, g_settings["square_pixels"] == "1" ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_soft, BM_SETCHECK, g_settings["soft_sound"] == "1" ? BST_CHECKED : BST_UNCHECKED, 0);

    ShowWindow(w, show);
    refresh();
    SetFocus(g_play);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(w, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    DeleteObject(g_font);
    DeleteObject(g_title_font);
    return 0;
}
