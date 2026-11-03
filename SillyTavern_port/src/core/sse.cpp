#include "core/sse.h"

namespace st {

std::vector<SseEvent> SseParser::feed(const char* data, std::size_t size) {
    std::vector<SseEvent> events;
    if (data == nullptr || size == 0) {
        return events;
    }
    buffer_.append(data, size);

    std::size_t start = 0;
    if (pending_cr_) {
        pending_cr_ = false;
        if (start < buffer_.size() && buffer_[start] == '\n') {
            ++start;
        }
    }
    while (start < buffer_.size()) {
        const std::size_t terminator = buffer_.find_first_of("\r\n", start);
        if (terminator == std::string::npos) {
            break;
        }
        const bool is_cr = buffer_[terminator] == '\r';
        if (is_cr && terminator + 1 == buffer_.size()) {
            // Consume the CR now but remember it: the next feed may begin with
            // the LF of a CRLF pair, which must count as the same terminator.
            process_line(buffer_.substr(start, terminator - start), events);
            start = terminator + 1;
            pending_cr_ = true;
            break;
        }
        process_line(buffer_.substr(start, terminator - start), events);
        std::size_t next = terminator + 1;
        if (is_cr && buffer_[next] == '\n') {
            ++next;
        }
        start = next;
    }
    buffer_.erase(0, start);
    return events;
}

std::vector<SseEvent> SseParser::feed(const std::string& data) {
    return feed(data.data(), data.size());
}

std::vector<SseEvent> SseParser::finish() {
    std::vector<SseEvent> events;
    pending_cr_ = false;
    if (!buffer_.empty()) {
        std::string line = buffer_;
        buffer_.clear();
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        process_line(line, events);
    }
    dispatch(events);
    return events;
}

void SseParser::reset() {
    buffer_.clear();
    event_name_.clear();
    data_.clear();
    has_data_ = false;
    pending_cr_ = false;
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
