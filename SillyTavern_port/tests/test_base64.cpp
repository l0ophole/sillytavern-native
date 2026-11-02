#include "test_util.h"

#include "core/base64.h"

#include <stdexcept>
#include <string>

TEST(base64_roundtrip) {
    CHECK_EQ(st::base64_encode("hello"), "aGVsbG8=");
    CHECK_EQ(st::base64_decode("aGVsbG8=", "test"), "hello");
}

TEST(base64_rejects_invalid_input) {
    for (const char* bad : {"a", "aGVsbG8", "aGVsbG8!", "====", "aGVsbG8=="}) {
        bool threw = false;
        try {
            st::base64_decode(bad, "test");
        } catch (const std::runtime_error&) {
            threw = true;
        }
        CHECK(threw);
    }
}

TEST(base64_handles_binary) {
    const std::string bytes("\x00\x01\xff", 3);
    CHECK_EQ(st::base64_decode(st::base64_encode(bytes), "test"), bytes);
}

TEST(base64_rejects_invalid_padding_bits) {
    bool threw = false;
    try {
        st::base64_decode("aGVsbG9=", "test");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}
