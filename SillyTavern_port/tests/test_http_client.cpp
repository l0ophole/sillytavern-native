#include "test_util.h"

#include "core/http_client.h"

#include <cstdlib>
#include <string>

TEST(http_rejects_invalid_url) {
    st::HttpClient client;
    st::HttpRequest request;
    request.url = "not-a-url";
    request.body = "{}";
    const auto response = client.post(request);
    CHECK(!response.error.empty());
    CHECK_EQ(response.status, 0L);
}

TEST(http_streams_data_callback) {
    // Skipped unless ST_MOCK_BASE_URL is set by integration_openai.sh.
    const char* base = std::getenv("ST_MOCK_BASE_URL");
    if (base == nullptr) {
        return;
    }
    st::HttpClient client;
    st::HttpRequest request;
    request.url = std::string(base) + "/chat/completions";
    request.headers = {"Content-Type: application/json"};
    request.body = R"({"stream":true})";
    std::string received;
    const auto response = client.post(request, [&](const char* data, std::size_t size) {
        received.append(data, size);
        return true;
    });
    CHECK_EQ(response.status, 200L);
    CHECK(response.error.empty());
    CHECK(received.find("data:") != std::string::npos);
}
