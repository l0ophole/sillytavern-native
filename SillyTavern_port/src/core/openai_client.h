#pragma once

#include "core/http_client.h"
#include "core/json.h"
#include "core/prompt.h"
#include "core/sampling.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace st {

struct OpenAiRequest {
    std::string base_url;
    std::string api_key;
    std::string model;
    std::vector<PromptMessage> messages;
    Sampling sampling;
    int timeout_seconds = 300;
    std::size_t max_response_bytes = 16u * 1024u * 1024u;
};

struct OpenAiResult {
    std::string text;
    std::string error;
    long status = 0;
    bool cancelled = false;
};

using OpenAiChunkCallback = std::function<void(const std::string&)>;

std::string openai_chat_url(const std::string& base_url);
Json openai_request_json(const OpenAiRequest& request);
std::string openai_error_message(long status, const std::string& body);

class OpenAiClient {
public:
    OpenAiResult generate(const OpenAiRequest& request, const OpenAiChunkCallback& on_chunk = {});
    void cancel();
    void reset_cancel();

private:
    HttpClient http_;
};

}  // namespace st
