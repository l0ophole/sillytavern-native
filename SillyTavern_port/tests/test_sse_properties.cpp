#include "test_util.h"

#include "core/sse.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using EventList = std::vector<std::pair<std::string, std::string>>;

std::string sample_stream() {
    return ": keepalive\r\n"
           "event: message\r\n"
           "data: line one\r\n"
           "data: line two\r\n"
           "\r\n"
           "data: cr\r"
           "\r"
           "data: lf\n"
           "\n"
           "data: utf8 \xE2\x9C\x93\n"
           "\n"
           "data: [DONE]\n"
           "\n";
}

void collect(EventList& target, const std::vector<st::SseEvent>& events) {
    for (const auto& event : events) {
        target.emplace_back(event.event, event.data);
    }
}

EventList one_feed(const std::string& stream) {
    st::SseParser parser;
    EventList events;
    collect(events, parser.feed(stream));
    collect(events, parser.finish());
    return events;
}

EventList split_feed(const std::string& stream, std::size_t split) {
    st::SseParser parser;
    EventList events;
    collect(events, parser.feed(stream.substr(0, split)));
    collect(events, parser.feed(stream.substr(split)));
    collect(events, parser.finish());
    return events;
}

void check_expected_events(const EventList& events) {
    CHECK_EQ(events.size(), std::size_t(5));
    if (events.size() != 5) {
        return;
    }
    CHECK_EQ(events[0].first, "message");
    CHECK_EQ(events[0].second, "line one\nline two");
    CHECK_EQ(events[1].first, "");
    CHECK_EQ(events[1].second, "cr");
    CHECK_EQ(events[2].second, "lf");
    CHECK_EQ(events[3].second, std::string("utf8 \xE2\x9C\x93"));
    CHECK_EQ(events[4].second, "[DONE]");
}

std::uint64_t xorshift(std::uint64_t& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

}  // namespace

TEST(sse_property_all_two_way_splits) {
    const std::string stream = sample_stream();
    const EventList expected = one_feed(stream);
    check_expected_events(expected);

    for (std::size_t split = 0; split <= stream.size(); ++split) {
        const EventList actual = split_feed(stream, split);
        CHECK_EQ(actual.size(), expected.size());
        if (actual.size() != expected.size()) {
            continue;
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            CHECK_EQ(actual[i].first, expected[i].first);
            CHECK_EQ(actual[i].second, expected[i].second);
        }
    }
}

TEST(sse_property_random_chunks) {
    const std::string stream = sample_stream();
    const EventList expected = one_feed(stream);
    check_expected_events(expected);

    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        st::SseParser parser;
        EventList actual;
        std::size_t offset = 0;
        while (offset < stream.size()) {
            std::size_t chunk = 1 + static_cast<std::size_t>(xorshift(state) % 17);
            if (chunk > stream.size() - offset) {
                chunk = stream.size() - offset;
            }
            collect(actual, parser.feed(stream.substr(offset, chunk)));
            offset += chunk;
        }
        collect(actual, parser.finish());
        CHECK(actual == expected);
        if (actual != expected) {
            break;
        }
    }
}
