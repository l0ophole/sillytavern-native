#include "test_util.h"

#include "core/openai_client.h"

#include <string>

TEST(openai_chat_url_variants) {
    CHECK_EQ(st::openai_chat_url("https://api.openai.com/v1"),
             "https://api.openai.com/v1/chat/completions");
    CHECK_EQ(st::openai_chat_url("https://api.openai.com/v1/"),
             "https://api.openai.com/v1/chat/completions");
    CHECK_EQ(st::openai_chat_url("http://localhost:1234/chat/completions"),
             "http://localhost:1234/chat/completions");
    CHECK_EQ(st::openai_chat_url("http://localhost:1234"),
             "http://localhost:1234/chat/completions");
    CHECK_EQ(st::openai_chat_url("http://localhost:1234/chat/completions/"),
             "http://localhost:1234/chat/completions");
}

TEST(openai_request_json_has_fields) {
    st::OpenAiRequest request;
    request.model = "gpt-4o-mini";
    request.messages = {{"system", "Be nice."}, {"user", "Hi"}};
    request.sampling.temperature = 0.7;
    request.sampling.max_tokens = 64;
    const auto value = st::openai_request_json(request);
    CHECK_EQ(value["model"].get<std::string>(), "gpt-4o-mini");
    CHECK_EQ(value["messages"].size(), std::size_t(2));
    CHECK_EQ(value["messages"][0]["role"].get<std::string>(), "system");
    CHECK_EQ(value["messages"][0]["content"].get<std::string>(), "Be nice.");
    CHECK_EQ(value["messages"][1]["content"].get<std::string>(), "Hi");
    CHECK_EQ(value["temperature"].get<double>(), 0.7);
    CHECK_EQ(value["max_tokens"].get<int>(), 64);
    CHECK_EQ(value["stream"].get<bool>(), true);
}

TEST(openai_request_json_clamps_sampling) {
    st::OpenAiRequest request;
    request.model = "mock";
    request.sampling.temperature = 10.0;
    request.sampling.top_p = -2.0;
    request.sampling.max_tokens = 0;
    const auto value = st::openai_request_json(request);
    CHECK_EQ(value["temperature"].get<double>(), 2.0);
    CHECK_EQ(value["top_p"].get<double>(), 0.0);
    CHECK_EQ(value["max_tokens"].get<int>(), 1);
}

TEST(openai_error_message_variants) {
    CHECK_EQ(st::openai_error_message(401, R"({"error":{"message":"bad key"}})"), "bad key");
    CHECK_EQ(st::openai_error_message(400, R"({"detail":"bad request"})"), "bad request");
    CHECK_EQ(st::openai_error_message(500, "oops"), "HTTP 500");
    CHECK_EQ(st::openai_error_message(404, ""), "HTTP 404");
    CHECK_EQ(st::openai_error_message(429, R"({"error":"rate limited"})"), "rate limited");
}
