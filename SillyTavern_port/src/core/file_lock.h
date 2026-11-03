#pragma once

#include <filesystem>
#include <optional>

namespace st {

// Advisory exclusive lock backed by flock(2). The lock is released when the
// object is destroyed or when the holding process exits.
class FileLock {
public:
    static std::optional<FileLock> try_acquire(const std::filesystem::path& path);
    static std::optional<long> holder_pid(const std::filesystem::path& path);

    FileLock() = default;
    ~FileLock();

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;

    bool holds_lock() const { return fd_ >= 0; }

private:
    explicit FileLock(int fd) : fd_(fd) {}
    void release();

    int fd_ = -1;
};

}  // namespace st
