#pragma once

#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <unistd.h>

class TempDir {
public:
    TempDir() {
        static std::atomic<unsigned long long> counter{0};
        path_ = std::filesystem::temp_directory_path() /
                ("st-test-" + std::to_string(static_cast<long long>(::getpid())) + "-" +
                 std::to_string(counter.fetch_add(1)));
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        if (!std::filesystem::create_directories(path_, error) || error) {
            throw std::runtime_error("cannot create temp dir " + path_.string() + ": " + error.message());
        }
    }

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};
