// SPDX-License-Identifier: MIT
// speaker.hpp - turns the timer interrupt's per-tick PIT divisors into PCM, like a PC speaker driven by PIT channel 2
// (square wave at 1193182 / divisor Hz, silent when the gate is off). The phase carries over between ticks, so a
// tone that continues across ticks has no clicks, as on the real hardware.
#pragma once

#include <cstdint>
#include <vector>

namespace ddhm {

class Speaker {
public:
    static constexpr double kPitHz = 1193181.666;
    static constexpr double kTickHz = kPitHz / 8519.0;   // the game's timer rate (divisor DS:0x2DFF = 0x2147)

    explicit Speaker(int sample_rate = 44100, int16_t amplitude = 6000) : rate_(sample_rate), amp_(amplitude) {}

    // Appends the samples for one timer tick with the given divisor (0 = speaker off).
    void tick(uint16_t divisor, std::vector<int16_t>& out) {
        acc_ += double(rate_) / kTickHz;
        const int n = int(acc_);
        acc_ -= n;
        if (divisor == 0) {
            for (int i = 0; i < n; ++i) out.push_back(shape(0));
            return;
        }
        const double step = (kPitHz / divisor) / rate_;   // cycles per sample
        for (int i = 0; i < n; ++i) {
            out.push_back(shape(phase_ < 0.5 ? amp_ : int16_t(-amp_)));
            phase_ += step;
            phase_ -= int(phase_);
        }
    }
    int rate() const { return rate_; }

    // Optional softer sound (not in the original): a two-pole low-pass at ~2.5 kHz takes the edge off the square wave,
    // the way a small speaker in a PC case did.
    bool soft = false;

private:
    int16_t shape(int16_t v) {
        if (!soft) { lp1_ = lp2_ = v; return v; }
        const double a = 0.30;
        lp1_ += a * (v - lp1_);
        lp2_ += a * (lp1_ - lp2_);
        return int16_t(lp2_);
    }
    double lp1_ = 0, lp2_ = 0;
    int rate_;
    int16_t amp_;
    double acc_ = 0, phase_ = 0;
};

}  // namespace ddhm
