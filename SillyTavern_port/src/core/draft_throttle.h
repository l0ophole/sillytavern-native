#pragma once

#include <chrono>
#include <cstddef>

namespace st {

class DraftThrottle {
public:
    explicit DraftThrottle(std::chrono::milliseconds base_interval = std::chrono::seconds(1))
        : base_interval_(base_interval) {}

    bool should_attempt(std::chrono::steady_clock::time_point now) const {
        if (!has_last_) {
            return true;
        }
        return now - last_attempt_ >= current_delay();
    }

    void note_saved(std::chrono::steady_clock::time_point now) {
        consecutive_failures_ = 0;
        last_attempt_ = now;
        has_last_ = true;
    }

    void note_failed(std::chrono::steady_clock::time_point now) {
        ++consecutive_failures_;
        last_attempt_ = now;
        has_last_ = true;
    }

    void reset() {
        consecutive_failures_ = 0;
        last_attempt_ = std::chrono::steady_clock::time_point{};
        has_last_ = false;
    }

private:
    std::chrono::milliseconds current_delay() const {
        if (consecutive_failures_ == 0) {
            return base_interval_;
        }
        int shift = static_cast<int>(consecutive_failures_);
        if (shift > 3) {
            shift = 3;
        }
        return base_interval_ * (1 << shift);
    }

    std::chrono::milliseconds base_interval_;
    std::chrono::steady_clock::time_point last_attempt_{};
    std::size_t consecutive_failures_ = 0;
    bool has_last_ = false;
};

}  // namespace st
