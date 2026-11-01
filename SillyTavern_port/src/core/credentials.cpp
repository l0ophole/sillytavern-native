#include "core/credentials.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <sys/stat.h>

namespace st {

void ensure_private_file(const std::filesystem::path& path) {
    if (::chmod(path.c_str(), 0600) != 0) {
        throw std::runtime_error("cannot restrict permissions on " + path.string() + ": " +
                                 std::strerror(errno));
    }
}

std::string mask_secret(const std::string& secret) {
    if (secret.empty()) {
        return {};
    }
    if (secret.size() <= 8) {
        return "****";
    }
    return secret.substr(0, 3) + "..." + secret.substr(secret.size() - 4);
}

bool is_secret_redacted(const std::string& text, const std::string& secret) {
    if (secret.empty()) {
        return true;
    }
    return text.find(secret) == std::string::npos;
}

}  // namespace st
