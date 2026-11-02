#include "core/sse.h"

namespace st {

std::vector<SseEvent> SseParser::feed(const char* data, std::size_t size) {
    std::vector<SseEvent> events;
    if (data == nullptr || size == 0) {
        return events;
    }
    buffer_.append(data, size);

    std::size_t start = 0;
    while (true) {
        const std::size_t newline = buffer_.find('\n', start);
        if (newline == std::string::npos) {
            break;
        }
        std::string line = buffer_.substr(start, newline - start);
        start = newline + 1;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        process_line(line, events);
    }
    buffer_.erase(0, start);
    return events;
}

std::vector<SseEvent> SseParser::feed(const std::string& data) {
    return feed(data.data(), data.size());
}

std::vector<SseEvent> SseParser::finish() {
    std::vector<SseEvent> events;
    if (!buffer_.empty()) {
        std::size_t start = 0;
        while (true) {
            const std::size_t newline = buffer_.find('\n', start);
            std::string line = newline == std::string::npos
                                   ? buffer_.substr(start)
                                   : buffer_.substr(start, newline - start);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            process_line(line, events);
            if (newline == std::string::npos) {
                break;
            }
            start = newline + 1;
        }
        buffer_.clear();
    }
    dispatch(events);
    return events;
}

void SseParser::reset() {
    buffer_.clear();
    event_name_.clear();
    data_.clear();
    has_data_ = false;
}

void SseParser::process_line(const std::string& line, std::vector<SseEvent>& events) {
    if (line.empty()) {
        dispatch(events);
        return;
    }
    if (line.front() == ':') {
        return;
    }

    const std::size_t colon = line.find(':');
    const std::string field = colon == std::string::npos ? line : line.substr(0, colon);
    std::string value;
    if (colon != std::string::npos) {
        value = line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') {
            value.erase(0, 1);
        }
    }

    if (field == "data") {
        if (has_data_) {
            data_ += '\n';
        }
        data_ += value;
        has_data_ = true;
    } else if (field == "event") {
        event_name_ = value;
    }
}

void SseParser::dispatch(std::vector<SseEvent>& events) {
    if (!has_data_) {
        event_name_.clear();
        return;
    }
    SseEvent event;
    event.event = event_name_;
    event.data = data_;
    events.push_back(std::move(event));
    event_name_.clear();
    data_.clear();
    has_data_ = false;
}

}  // namespace st
