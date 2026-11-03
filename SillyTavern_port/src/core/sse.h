#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace st {

struct SseEvent {
    std::string event;
    std::string data;
};

class SseParser {
public:
    explicit SseParser(std::size_t max_event_bytes = 16u * 1024u * 1024u);

    std::vector<SseEvent> feed(const char* data, std::size_t size);
    std::vector<SseEvent> feed(const std::string& data);
    std::vector<SseEvent> finish();
    void reset();

    bool failed() const { return failed_; }
    const std::string& error() const { return error_; }

private:
    void process_line(const std::string& line, std::vector<SseEvent>& events);
    void dispatch(std::vector<SseEvent>& events);
    void fail();

    std::size_t max_event_bytes_;
    bool failed_ = false;
    std::string error_;
    std::string buffer_;
    std::string event_name_;
    std::string data_;
    bool has_data_ = false;
    bool pending_cr_ = false;
};

}  // namespace st
