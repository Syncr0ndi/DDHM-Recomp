// SPDX-License-Identifier: MIT
// machine.hpp - the parts of the PC the game talks to, rebuilt as plain C++ state: the keyboard interrupt's key table,
// the 140 Hz timer interrupt with its PC-speaker sound player, the vertical-retrace wait and the screen.
//
// The original program is written as blocking code (wait for a key, wait n retraces, loop until a timer counter
// reaches a value). The port keeps that shape: the game program runs on its own thread and every wait_vbl() hands
// control back to the front end, which presents the screen, delivers input, advances the timer two ticks (140 Hz /
// 70 Hz) and resumes it. Exactly one side runs at any time, so the program is deterministic and needs no locking
// beyond the hand-over itself.
#pragma once

#include <array>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "dd2/exe.hpp"
#include "game/screen.hpp"

namespace ddhm {

// PC keyboard scan codes (set 1) used by the original.
namespace sc {
constexpr int kEsc = 0x01, k1 = 0x02, k8 = 0x09, kBackspace = 0x0E, kTab = 0x0F, kY = 0x15, kEnter = 0x1C,
              kCtrl = 0x1D, kN = 0x31, kAlt = 0x38, kSpace = 0x39, kF1 = 0x3B, kF2 = 0x3C, kF3 = 0x3D, kF4 = 0x3E,
              kF5 = 0x3F, kF10 = 0x44, kUp = 0x48, kPgUp = 0x49, kLeft = 0x4B, kRight = 0x4D, kHome = 0x47,
              kEnd = 0x4F, kDown = 0x50, kPgDn = 0x51, kF11 = 0x57, kF12 = 0x58;
}

struct QuitProgram {};   // thrown inside the program thread when the front end shuts down

class Machine {
public:
    explicit Machine(const dd2::ExeData& exe);
    ~Machine();
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    const dd2::ExeData& exe;
    Screen screen;

    // ---- Keyboard interrupt (0x8ED1) ----
    std::array<uint8_t, 128> keydown{};      // DS:0x2CF6, 1 while held
    int last_scan = 0;                       // DS:0x2D76: scan code of the last press, bit 7 = not yet consumed
    int last_ascii = 0;                      // DS:0x2D78: its ASCII value (table at DS:0x2D7A)
    void key_event(int scancode, bool down); // front end -> "interrupt"
    void clear_keys();                       // 0x78DF
    int poll_key();                          // 0x8E99(1): ASCII of the last key (0 = none), does not consume it

    // ---- Joystick / gamepad (optional, the front end fills these) ----
    bool pad_connected = false;
    int pad_dir = 8;                         // 0..7 clockwise from up, 8 = centred
    bool pad_b1 = false, pad_b2 = false;

    // ---- Timer interrupt (0x904B) ----
    uint32_t ticks = 0;                      // DS:0x2E01, 140 Hz
    std::function<void()> tick_hook;         // DS:0x2E1B, called every tick while set
    void timer_tick();                       // front end -> "interrupt"

    // ---- PC speaker (0x8FA8 play, 0x8FDE stop, 0x9033 wait) ----
    bool sound_enabled = true;               // DS:0x2E0B (F2)
    void play_sound(int n);
    void stop_sound();
    bool sound_playing() const { return snd_ != nullptr; }
    std::vector<uint16_t> speaker;           // PIT divisor per elapsed tick (0 = speaker off); drained by the front end

    // ---- Program side ----
    void wait_vbl();                         // one vertical retrace (0x91B9 with n = 1)
    void delay(int retraces) { while (retraces-- > 0) wait_vbl(); }
    void wait_sound();                       // 0x9033: until the current sound ends

    // ---- Front-end side ----
    void start(std::function<void(Machine&)> program);
    bool run_frame();                        // two timer ticks, then run the program to its next wait_vbl; false = it ended
    bool finished() const { return done_; }
    uint64_t frame = 0;                      // retraces so far

private:
    const dd2::Sound* snd_ = nullptr;
    size_t snd_pos_ = 0;
    int snd_priority_ = 0;

    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool program_turn_ = false, done_ = false, quit_ = false;
};

}  // namespace ddhm
