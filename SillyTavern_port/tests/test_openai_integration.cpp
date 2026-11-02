#include "test_util.h"

#include "core/openai_client.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

TEST(openai_streams_from_mock_server) {
    const char* base = std::getenv("ST_MOCK_BASE_URL");
    if (base == nullptr) {
        return;
    }
    st::OpenAiClient client;
    st::OpenAiRequest request;
    request.base_url = base;
    request.model = "mock";
    request.messages = {{"user", "Hi"}};
    std::string chunks;
    const auto result = client.generate(request, [&](const std::string& chunk) { chunks += chunk; });
    CHECK(result.error.empty());
    CHECK_EQ(result.status, 200L);
    CHECK(!result.cancelled);
    CHECK_EQ(result.text, "hello world");
    CHECK_EQ(chunks, "hello world");
}

TEST(openai_non_streaming_from_mock_server) {
    const char* base = std::getenv("ST_MOCK_BASE_URL");
    if (base == nullptr) {
        return;
    }
    st::OpenAiClient client;
    st::OpenAiRequest request;
    request.base_url = base;
    request.model = "mock";
    request.messages = {{"user", "Hi"}};
    request.sampling.stream = false;
    const auto result = client.generate(request);
    CHECK(result.error.empty());
    CHECK_EQ(result.status, 200L);
    CHECK_EQ(result.text, "hello");
}

TEST(openai_reports_mock_error) {
    const char* base = std::getenv("ST_MOCK_BASE_URL");
    if (base == nullptr) {
        return;
    }
    st::OpenAiClient client;
    st::OpenAiRequest request;
    request.base_url = std::string(base) + "/error";
    request.model = "mock";
    request.messages = {{"user", "Hi"}};
    const auto result = client.generate(request);
    CHECK_EQ(result.status, 401L);
    CHECK_EQ(result.error, "bad key");
}

TEST(openai_reports_malformed_stream) {
    const char* base = std::getenv("ST_MOCK_BASE_URL");
    if (base == nullptr) {
        return;
    }
    st::OpenAiClient client;
    st::OpenAiRequest request;
    request.base_url = std::string(base) + "/malformed";
    request.model = "mock";
    request.messages = {{"user", "Hi"}};
    const auto result = client.generate(request);
    CHECK(!result.error.empty());
    CHECK(result.text.empty());
}

TEST(openai_keeps_partial_text_on_stream_error) {
    const char* base = std::getenv("ST_MOCK_BASE_URL");
    if (base == nullptr) {
        return;
    }
    st::OpenAiClient client;
    st::OpenAiRequest request;
    request.base_url = std::string(base) + "/partial";
    request.model = "mock";
    request.messages = {{"user", "Hi"}};
    const auto result = client.generate(request);
    CHECK(!result.error.empty());
    CHECK_EQ(result.text, "hello");
}

TEST(openai_reports_mid_stream_error) {
    const char* base = std::getenv("ST_MOCK_BASE_URL");
    if (base == nullptr) {
        return;
    }
    st::OpenAiClient client;
    st::OpenAiRequest request;
    request.base_url = std::string(base) + "/stream-error";
    request.model = "mock";
    request.messages = {{"user", "Hi"}};
    const auto result = client.generate(request);
    CHECK_EQ(result.text, "hello");
    CHECK_EQ(result.error, "boom");
}

TEST(openai_cancels_slow_stream) {
    const char* base = std::getenv("ST_MOCK_BASE_URL");
    if (base == nullptr) {
        return;
    }
    st::OpenAiClient client;
    st::OpenAiRequest request;
    request.base_url = std::string(base) + "/slow";
    request.model = "mock";
    request.messages = {{"user", "Hi"}};
    std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        client.cancel();
    });
    const auto result = client.generate(request);
    canceller.join();
    CHECK(result.cancelled);
}
