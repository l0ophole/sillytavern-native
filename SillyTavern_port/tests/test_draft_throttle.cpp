#include "test_util.h"

#include "core/draft_throttle.h"

#include <chrono>

TEST(draft_throttle_allows_first_attempt) {
    st::DraftThrottle throttle(std::chrono::milliseconds(1000));
    CHECK(throttle.should_attempt(std::chrono::steady_clock::time_point{}));
}

TEST(draft_throttle_suppresses_within_interval) {
    st::DraftThrottle throttle(std::chrono::milliseconds(1000));
    const auto start = std::chrono::steady_clock::time_point{};
    throttle.note_saved(start);
    CHECK(!throttle.should_attempt(start + std::chrono::milliseconds(999)));
    CHECK(throttle.should_attempt(start + std::chrono::milliseconds(1000)));
}

TEST(draft_throttle_failure_backs_off_and_success_resets) {
    st::DraftThrottle throttle(std::chrono::milliseconds(1000));
    const auto start = std::chrono::steady_clock::time_point{};
    throttle.note_failed(start);
    CHECK(!throttle.should_attempt(start + std::chrono::milliseconds(1500)));
    CHECK(throttle.should_attempt(start + std::chrono::milliseconds(2000)));
    throttle.note_saved(start + std::chrono::milliseconds(2000));
    CHECK(throttle.should_attempt(start + std::chrono::milliseconds(3000)));
}
