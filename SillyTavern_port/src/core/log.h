#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace st {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

void log_init(const std::filesystem::path& log_file, bool also_stderr = true);
void log_shutdown();
void log_write(LogLevel level, const std::string& message);
void log_set_redactions(std::vector<std::string> secrets);

std::string redact_secrets(std::string text, const std::vector<std::string>& secrets);

}  // namespace st
