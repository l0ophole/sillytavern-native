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
    std::vector<SseEvent> feed(const char* data, std::size_t size);
    std::vector<SseEvent> feed(const std::string& data);
    std::vector<SseEvent> finish();
    void reset();

private:
    void process_line(const std::string& line, std::vector<SseEvent>& events);
    void dispatch(std::vector<SseEvent>& events);

    std::string buffer_;
    std::string event_name_;
    std::string data_;
    bool has_data_ = false;
};

}  // namespace st
