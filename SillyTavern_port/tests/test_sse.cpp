#include "test_util.h"

#include "core/sse.h"

#include <string>
#include <vector>

TEST(sse_parses_split_chunks) {
    st::SseParser parser;
    std::vector<st::SseEvent> events;
    const std::string first = "data: hel";
    const std::string second = "lo\n\n";
    for (const auto& event : parser.feed(first)) events.push_back(event);
    for (const auto& event : parser.feed(second)) events.push_back(event);
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].data, "hello");
}

TEST(sse_parses_multiline_data_and_crlf) {
    st::SseParser parser;
    const auto events = parser.feed("event: message\r\ndata: a\r\ndata: b\r\n\r\n");
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].event, "message");
    CHECK_EQ(events[0].data, "a\nb");
}

TEST(sse_ignores_comments_and_handles_done) {
    st::SseParser parser;
    const auto events = parser.feed(": keepalive\ndata: [DONE]\n\n");
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].data, "[DONE]");
}

TEST(sse_finish_dispatches_pending_event) {
    st::SseParser parser;
    CHECK_EQ(parser.feed("data: tail").size(), std::size_t(0));
    const auto events = parser.finish();
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].data, "tail");
}

TEST(sse_reset_clears_state) {
    st::SseParser parser;
    (void)parser.feed("data: partial");
    parser.reset();
    const auto events = parser.feed("data: fresh\n\n");
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].data, "fresh");
}

TEST(sse_feed_bytes_handles_embedded_nul) {
    st::SseParser parser;
    const char chunk[] = {'d', 'a', 't', 'a', ':', ' ', 'a', '\0', 'b', '\n', '\n'};
    const auto events = parser.feed(chunk, sizeof(chunk));
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].data, std::string("a\0b", 3));
}
