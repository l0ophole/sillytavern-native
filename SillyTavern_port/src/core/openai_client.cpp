#include "core/openai_client.h"

#include "core/sse.h"

#include <cstddef>
#include <exception>
#include <string>

namespace st {
namespace {

struct StreamState {
    std::string text;
    std::string error;
    bool done = false;
    const OpenAiChunkCallback* on_chunk = nullptr;
};

bool handle_event(const SseEvent& event, StreamState& state) {
    if (event.data == "[DONE]") {
        state.done = true;
        return false;
    }

    try {
        const Json value = parse_json(event.data, "stream event");
        if (!value.is_object() || !value.contains("choices") || !value["choices"].is_array() ||
            value["choices"].empty()) {
            return true;
        }
        const Json& choice = value["choices"][0];
        if (!choice.is_object() || !choice.contains("delta") || !choice["delta"].is_object()) {
            return true;
        }
        const Json& delta = choice["delta"];
        if (!delta.contains("content") || !delta["content"].is_string()) {
            return true;
        }
        const std::string chunk = delta["content"].get<std::string>();
        if (!chunk.empty()) {
            state.text += chunk;
            if (state.on_chunk != nullptr && *state.on_chunk) {
                (*state.on_chunk)(chunk);
            }
        }
    } catch (const std::exception& error) {
        state.error = std::string("malformed stream event: ") + error.what();
        return false;
    }
    return true;
}

bool extract_message_content(const std::string& body, std::string& text) {
    const Json value = parse_json(body, "response");
    if (!value.is_object() || !value.contains("choices") || !value["choices"].is_array() ||
        value["choices"].empty()) {
        return false;
    }
    const Json& choice = value["choices"][0];
    if (!choice.is_object() || !choice.contains("message") || !choice["message"].is_object()) {
        return false;
    }
    const Json& message = choice["message"];
    if (!message.contains("content") || !message["content"].is_string()) {
        return false;
    }
    text = message["content"].get<std::string>();
    return true;
}

std::string strip_trailing_slashes(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

}  // namespace

std::string openai_chat_url(const std::string& base_url) {
    std::string url = strip_trailing_slashes(base_url);
    const std::string suffix = "/chat/completions";
    if (url.size() >= suffix.size() &&
        url.compare(url.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return url;
    }
    return url + suffix;
}

Json openai_request_json(const OpenAiRequest& request) {
    Json value = Json::object();
    value["model"] = request.model;

    Json messages = Json::array();
    for (const PromptMessage& message : request.messages) {
        Json item = Json::object();
        item["role"] = message.role;
        item["content"] = message.content;
        messages.push_back(std::move(item));
    }
    value["messages"] = std::move(messages);

    const Json sampling = sampling_json(request.sampling);
    value["temperature"] = sampling["temperature"];
    value["top_p"] = sampling["top_p"];
    value["max_tokens"] = sampling["max_tokens"];
    value["stream"] = sampling["stream"];
    return value;
}

std::string openai_error_message(long status, const std::string& body) {
    try {
        const Json value = parse_json(body, "error response");
        if (value.is_object()) {
            if (value.contains("error")) {
                const Json& error = value["error"];
                if (error.is_object() && error.contains("message") &&
                    error["message"].is_string()) {
                    return error["message"].get<std::string>();
                }
                if (error.is_string()) {
                    return error.get<std::string>();
                }
            }
            if (value.contains("detail") && value["detail"].is_string()) {
                return value["detail"].get<std::string>();
            }
        }
    } catch (const std::exception&) {
        // Fall through to the status-only message.
    }
    return "HTTP " + std::to_string(status);
}

OpenAiResult OpenAiClient::generate(const OpenAiRequest& request,
                                    const OpenAiChunkCallback& on_chunk) {
    OpenAiResult result;

    HttpRequest http_request;
    http_request.url = openai_chat_url(request.base_url);
    http_request.body = dump_json(openai_request_json(request));
    http_request.headers = {"Content-Type: application/json"};
    http_request.headers.push_back(request.sampling.stream ? "Accept: text/event-stream"
                                                           : "Accept: application/json");
    if (!request.api_key.empty()) {
        http_request.headers.push_back("Authorization: Bearer " + request.api_key);
    }
    http_request.timeout_seconds = request.timeout_seconds;

    SseParser parser;
    StreamState state;
    state.on_chunk = &on_chunk;

    HttpDataCallback data_callback;
    if (request.sampling.stream) {
        data_callback = [&parser, &state](const char* data, std::size_t size) {
            for (const SseEvent& event : parser.feed(data, size)) {
                if (!handle_event(event, state)) {
                    return false;
                }
            }
            return true;
        };
    }

    const HttpResponse response = http_.post(http_request, data_callback);
    result.text = state.text;
    result.status = response.status;

    if (response.cancelled) {
        result.cancelled = true;
        return result;
    }
    if (state.done) {
        return result;
    }

    if (request.sampling.stream && response.status < 400 && response.error.empty() &&
        state.error.empty()) {
        for (const SseEvent& event : parser.finish()) {
            if (!handle_event(event, state)) {
                break;
            }
        }
        result.text = state.text;
        if (state.done) {
            return result;
        }
    }

    if (response.status >= 400) {
        result.error = openai_error_message(response.status, response.body);
        return result;
    }
    if (!state.error.empty()) {
        result.error = state.error;
        return result;
    }
    if (!response.error.empty()) {
        result.error = response.error;
        return result;
    }

    if (result.text.empty()) {
        const std::size_t first = response.body.find_first_not_of(" \t\r\n");
        const bool looks_like_json = first != std::string::npos && response.body[first] == '{';
        if (looks_like_json) {
            try {
                if (!extract_message_content(response.body, result.text)) {
                    result.error = "response missing choices[0].message.content";
                }
            } catch (const std::exception& error) {
                result.error = error.what();
            }
        } else if (!request.sampling.stream) {
            result.error = response.body.empty() ? "empty response body"
                                                 : "response is not valid JSON";
        }
    }
    return result;
}

void OpenAiClient::cancel() {
    http_.cancel();
}

}  // namespace st
