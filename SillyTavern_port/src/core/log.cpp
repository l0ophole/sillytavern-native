#include "core/log.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace st {
namespace {

std::mutex& log_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::ofstream& log_stream() {
    static std::ofstream stream;
    return stream;
}

bool& log_stderr() {
    static bool enabled = true;
    return enabled;
}

std::vector<std::string>& redactions() {
    static std::vector<std::string> secrets;
    return secrets;
}

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "INFO";
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local {};
    localtime_r(&seconds, &local);
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%dT%H:%M:%S");
    return out.str();
}

}  // namespace

std::string redact_secrets(std::string text, const std::vector<std::string>& secrets) {
    for (const auto& secret : secrets) {
        if (secret.empty()) {
            continue;
        }
        std::size_t position = 0;
        while ((position = text.find(secret, position)) != std::string::npos) {
            text.replace(position, secret.size(), "[REDACTED]");
            position += 10;
        }
    }
    return text;
}

void log_init(const std::filesystem::path& log_file, bool also_stderr) {
    std::lock_guard<std::mutex> lock(log_mutex());
    if (log_stream().is_open()) {
        log_stream().close();
    }
    if (!log_file.parent_path().empty()) {
        std::error_code error;
        std::filesystem::create_directories(log_file.parent_path(), error);
        if (error) {
            throw std::runtime_error("cannot create log directory " + log_file.parent_path().string() +
                                     ": " + error.message());
        }
    }
    log_stream().open(log_file, std::ios::app);
    if (!log_stream()) {
        throw std::runtime_error("cannot open log file " + log_file.string());
    }
    std::error_code perm_error;
    std::filesystem::permissions(log_file,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace,
                                 perm_error);
    if (perm_error) {
        log_stream().close();
        throw std::runtime_error("cannot secure log file " + log_file.string() + ": " +
                                 perm_error.message());
    }
    log_stderr() = also_stderr;
}

void log_shutdown() {
    std::lock_guard<std::mutex> lock(log_mutex());
    if (log_stream().is_open()) {
        log_stream().flush();
        log_stream().close();
    }
}

void log_set_redactions(std::vector<std::string> secrets) {
    std::lock_guard<std::mutex> lock(log_mutex());
    redactions() = std::move(secrets);
}

void log_write(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex());
    const std::string safe = redact_secrets(message, redactions());
    std::ostringstream line;
    line << '[' << timestamp() << "] [" << level_name(level) << "] " << safe << '\n';
    if (log_stderr()) {
        std::cerr << line.str();
        std::cerr.flush();
    }
    if (log_stream().is_open()) {
        log_stream() << line.str();
        log_stream().flush();
    }
}

}  // namespace st
