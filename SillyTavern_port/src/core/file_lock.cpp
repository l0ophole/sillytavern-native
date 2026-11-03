#include "core/file_lock.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace st {
namespace {

std::string errno_message(const std::string& action, const fs::path& path) {
    return action + " " + path.string() + ": " + std::strerror(errno);
}

}  // namespace

std::optional<FileLock> FileLock::try_acquire(const fs::path& path) {
    const fs::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        fs::create_directories(parent, error);
        if (error) {
            throw std::runtime_error("cannot create directory " + parent.string() + ": " +
                                     error.message());
        }
    }

    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        throw std::runtime_error(errno_message("cannot open lock file", path));
    }

    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int lock_error = errno;
        ::close(fd);
        if (lock_error == EWOULDBLOCK || lock_error == EAGAIN) {
            return std::nullopt;
        }
        errno = lock_error;
        throw std::runtime_error(errno_message("cannot lock", path));
    }

    if (::ftruncate(fd, 0) != 0) {
        const int truncate_error = errno;
        ::flock(fd, LOCK_UN);
        ::close(fd);
        errno = truncate_error;
        throw std::runtime_error(errno_message("cannot truncate lock file", path));
    }

    const std::string contents =
        "pid " + std::to_string(static_cast<long>(::getpid())) + "\n";
    std::size_t written = 0;
    while (written < contents.size()) {
        const ssize_t result = ::write(fd, contents.data() + written, contents.size() - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int write_error = errno;
            ::flock(fd, LOCK_UN);
            ::close(fd);
            errno = write_error;
            throw std::runtime_error(errno_message("cannot write lock file", path));
        }
        written += static_cast<std::size_t>(result);
    }

    return FileLock(fd);
}

std::optional<long> FileLock::holder_pid(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }
    std::string line;
    if (!std::getline(in, line)) {
        return std::nullopt;
    }
    constexpr const char* kPrefix = "pid ";
    if (line.rfind(kPrefix, 0) != 0) {
        return std::nullopt;
    }
    const std::string digits = line.substr(std::strlen(kPrefix));
    if (digits.empty() ||
        !std::all_of(digits.begin(), digits.end(),
                     [](unsigned char value) { return std::isdigit(value) != 0; })) {
        return std::nullopt;
    }
    try {
        return std::stol(digits);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

FileLock::FileLock(FileLock&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
        release();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

FileLock::~FileLock() {
    release();
}

void FileLock::release() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
        fd_ = -1;
    }
}

}  // namespace st
