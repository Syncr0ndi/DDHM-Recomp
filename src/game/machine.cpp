// SPDX-License-Identifier: MIT
#include "game/machine.hpp"

namespace ddhm {

Machine::Machine(const dd2::ExeData& e) : exe(e), screen(&e) {}

Machine::~Machine() {
    if (thread_.joinable()) {
        {
            std::unique_lock<std::mutex> lk(mu_);
            quit_ = true;
            program_turn_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }
}

void Machine::key_event(int scancode, bool down) {
    scancode &= 0x7F;
    if (!down) { keydown[size_t(scancode)] = 0; return; }
    last_scan = scancode | 0x80;
    keydown[size_t(scancode)] = 1;
    last_ascii = exe.ds_byte(uint16_t(0x2D7A + scancode));
}

void Machine::clear_keys() {
    last_ascii = 0;
    last_scan = 0;
    keydown.fill(0);
}

int Machine::poll_key() { return last_ascii; }

void Machine::timer_tick() {
    ++ticks;
    if (tick_hook) tick_hook();
    uint16_t out = 0;
    if (snd_) {
        if (snd_pos_ >= snd_->divisors.size()) stop_sound();         // the 0xFFFF terminator
        else {
            const uint16_t v = snd_->divisors[snd_pos_++];
            out = sound_enabled ? v : 0;
        }
    }
    speaker.push_back(out);
}

void Machine::play_sound(int n) {
    if (n <= 0 || size_t(n) >= exe.sounds.size()) return;
    const dd2::Sound& s = exe.sounds[size_t(n)];
    if (s.priority < snd_priority_) return;
    snd_priority_ = s.priority;
    snd_ = &s;
    snd_pos_ = 0;
}

void Machine::stop_sound() {
    snd_ = nullptr;
    snd_pos_ = 0;
    snd_priority_ = 0;
}

void Machine::wait_sound() {
    while (snd_) wait_vbl();
}

void Machine::wait_vbl() {
    std::unique_lock<std::mutex> lk(mu_);
    program_turn_ = false;
    cv_.notify_all();
    cv_.wait(lk, [&] { return program_turn_; });
    if (quit_) throw QuitProgram{};
}

void Machine::start(std::function<void(Machine&)> program) {
    program_turn_ = false;
    thread_ = std::thread([this, program] {
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return program_turn_; });
        }
        try {
            if (!quit_) program(*this);
        } catch (const QuitProgram&) {
        }
        std::unique_lock<std::mutex> lk(mu_);
        done_ = true;
        program_turn_ = false;
        cv_.notify_all();
    });
}

bool Machine::run_frame() {
    if (done_) return false;
    timer_tick();
    timer_tick();
    ++frame;
    std::unique_lock<std::mutex> lk(mu_);
    program_turn_ = true;
    cv_.notify_all();
    cv_.wait(lk, [&] { return !program_turn_; });
    return !done_;
}

}  // namespace ddhm
