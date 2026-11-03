#include "test_util.h"

#include "core/openai_client.h"

#include <cstdlib>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace {

// Minimal one-shot HTTP server so the streaming test stays independent of the
// Python mock server used by the integration suite.
class LoopbackSseServer {
public:
    explicit LoopbackSseServer(std::string body) : body_(std::move(body)) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            return;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            ::listen(listen_fd_, 1) != 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return;
        }
        socklen_t address_size = sizeof(address);
        if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &address_size) != 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return;
        }
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] { serve(); });
    }

    ~LoopbackSseServer() {
        if (thread_.joinable()) {
            thread_.join();
        }
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
        }
    }

    LoopbackSseServer(const LoopbackSseServer&) = delete;
    LoopbackSseServer& operator=(const LoopbackSseServer&) = delete;

    bool ok() const { return listen_fd_ >= 0 && port_ != 0; }

    std::string base_url() const {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

private:
    void serve() {
        pollfd entry{listen_fd_, POLLIN, 0};
        if (::poll(&entry, 1, 5000) <= 0) {
            return;
        }
        const int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) {
            return;
        }
        read_request(fd);
        const std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Content-Length: " +
            std::to_string(body_.size()) +
            "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            body_;
        std::size_t sent = 0;
        while (sent < response.size()) {
            const ssize_t written =
                ::send(fd, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
            if (written <= 0) {
                break;
            }
            sent += static_cast<std::size_t>(written);
        }
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }

    void read_request(int fd) {
        std::string request;
        char buffer[4096];
        while (request.find("\r\n\r\n") == std::string::npos) {
            const ssize_t received = ::recv(fd, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                return;
            }
            request.append(buffer, static_cast<std::size_t>(received));
        }
        const std::size_t header_end = request.find("\r\n\r\n") + 4;
        std::size_t content_length = 0;
        const std::string key = "Content-Length: ";
        const std::size_t position = request.find(key);
        if (position != std::string::npos) {
            content_length = std::strtoul(request.c_str() + position + key.size(), nullptr, 10);
        }
        while (request.size() - header_end < content_length) {
            const ssize_t received = ::recv(fd, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                return;
            }
            request.append(buffer, static_cast<std::size_t>(received));
        }
    }

    std::string body_;
    int listen_fd_ = -1;
    int port_ = 0;
    std::thread thread_;
};

}  // namespace

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

TEST(openai_bounds_aggregate_streamed_response) {
    LoopbackSseServer server(
        "data: {\"choices\":[{\"delta\":{\"content\":\"hello\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\" world\"}}]}\n\n"
        "data: [DONE]\n\n");
    CHECK(server.ok());
    if (!server.ok()) {
        return;
    }

    st::OpenAiClient client;
    st::OpenAiRequest request;
    request.base_url = server.base_url();
    request.model = "mock";
    request.messages = {{"user", "Hi"}};
    request.max_response_bytes = 10;

    const auto result = client.generate(request);
    CHECK(!result.error.empty());
    CHECK(result.error.find("exceeds") != std::string::npos);
    CHECK_EQ(result.text, "hello");
    CHECK(!result.cancelled);
    CHECK_EQ(result.status, 200L);
}
