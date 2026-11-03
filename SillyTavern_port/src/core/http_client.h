#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace st {

struct HttpRequest {
    std::string url;
    std::vector<std::string> headers;
    std::string body;
    long timeout_seconds = 300;
    long connect_timeout_seconds = 15;
    bool follow_redirects = true;
    bool buffer_response_body = true;
    std::size_t max_response_bytes = 16u * 1024u * 1024u;
    std::size_t diagnostic_body_bytes = 1024u * 1024u;
};

struct HttpResponse {
    long status = 0;
    std::string body;
    std::string error;
    bool cancelled = false;
};

using HttpDataCallback = std::function<bool(const char* data, std::size_t size)>;

class HttpClient {
public:
    HttpClient() = default;
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResponse post(const HttpRequest& request, const HttpDataCallback& on_data = {});
    void cancel();
    void reset_cancel();

private:
    std::atomic<bool> cancelled_{false};
};

}  // namespace st
