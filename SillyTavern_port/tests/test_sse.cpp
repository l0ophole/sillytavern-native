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

TEST(sse_finish_handles_multiple_lines) {
    st::SseParser parser;
    CHECK_EQ(parser.feed("data: a\ndata: b").size(), std::size_t(0));
    const auto events = parser.finish();
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].data, "a\nb");
}

TEST(sse_reset_clears_state) {
    st::SseParser parser;
    (void)parser.feed("data: partial");
    parser.reset();
    const auto events = parser.feed("data: fresh\n\n");
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].data, "fresh");
}

TEST(sse_handles_cr_only_line_endings) {
    st::SseParser parser;
    const auto events = parser.feed("data: hello\r\r");
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].data, "hello");
}

TEST(sse_handles_split_crlf) {
    st::SseParser parser;
    CHECK_EQ(parser.feed("data: hello\r").size(), std::size_t(0));
    const auto events = parser.feed("\n\r\n");
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].data, "hello");
}

TEST(sse_rejects_oversized_unterminated_line) {
    st::SseParser parser(16);
    CHECK(parser.feed("data: hello ").empty());
    CHECK(!parser.failed());
    const auto events = parser.feed("world");
    CHECK(events.empty());
    CHECK(parser.failed());
    CHECK(parser.error().find("exceeds") != std::string::npos);
    CHECK(parser.finish().empty());
    parser.reset();
    CHECK(!parser.failed());
    CHECK(parser.error().empty());
}

TEST(sse_rejects_oversized_multiline_data) {
    st::SseParser parser(24);
    CHECK(parser.feed("data: 123456789\n").empty());
    CHECK(parser.feed("data: 123456789\n").empty());
    CHECK(!parser.failed());
    const auto events = parser.feed("data: 123456789\n");
    CHECK(events.empty());
    CHECK(parser.failed());
    CHECK(parser.error().find("exceeds") != std::string::npos);
    CHECK(parser.finish().empty());
}

TEST(sse_feed_bytes_handles_embedded_nul) {
    st::SseParser parser;
    const char chunk[] = {'d', 'a', 't', 'a', ':', ' ', 'a', '\0', 'b', '\n', '\n'};
    const auto events = parser.feed(chunk, sizeof(chunk));
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].data, std::string("a\0b", 3));
}

TEST(sse_incremental_scan_handles_line_split_across_many_feeds) {
    st::SseParser parser;
    std::string body;
    for (int i = 0; i < 256; ++i) {
        parser.feed(std::string(4096, 'a'));
        body.append(4096, 'a');
    }
    CHECK_EQ(body.size(), std::size_t(256 * 4096));
    const auto events = parser.feed(": value\n\ndata: done\n\n");
    CHECK(!parser.failed());
    CHECK_EQ(events.size(), std::size_t(1));
    if (events.size() == 1) {
        CHECK_EQ(events[0].data, "done");
    }
}

TEST(sse_incremental_scan_oversized_unterminated_line_still_fails) {
    st::SseParser parser;
    const std::string chunk(1024 * 1024, 'a');
    for (int i = 0; i < 17; ++i) {
        (void)parser.feed(chunk);
    }
    CHECK(parser.failed());
    CHECK(parser.error().find("exceeds") != std::string::npos);
    CHECK(parser.finish().empty());
}
