#include "core/atomic_file.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace st {
namespace {

mode_t mode_from_permissions(fs::perms permissions) {
    mode_t mode = 0;
    const auto has = [&](fs::perms bit) { return (permissions & bit) != fs::perms::none; };
    if (has(fs::perms::owner_read)) mode |= S_IRUSR;
    if (has(fs::perms::owner_write)) mode |= S_IWUSR;
    if (has(fs::perms::owner_exec)) mode |= S_IXUSR;
    if (has(fs::perms::group_read)) mode |= S_IRGRP;
    if (has(fs::perms::group_write)) mode |= S_IWGRP;
    if (has(fs::perms::group_exec)) mode |= S_IXGRP;
    if (has(fs::perms::others_read)) mode |= S_IROTH;
    if (has(fs::perms::others_write)) mode |= S_IWOTH;
    if (has(fs::perms::others_exec)) mode |= S_IXOTH;
    return mode;
}

std::string errno_message(const std::string& action, const fs::path& path) {
    return action + " " + path.string() + ": " + std::strerror(errno);
}

}  // namespace

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::ostringstream out;
    out << in.rdbuf();
    if (!in.good() && !in.eof()) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return out.str();
}

bool file_exists(const fs::path& path) {
    std::error_code error;
    return fs::exists(path, error);
}

void write_file_atomic(const fs::path& path, const std::string& data, fs::perms permissions) {
    if (path.empty()) {
        throw std::runtime_error("cannot write an empty path");
    }

    static std::atomic<unsigned long long> counter{0};
    const fs::path parent = path.parent_path().empty() ? fs::path(".") : path.parent_path();
    std::error_code directory_error;
    fs::create_directories(parent, directory_error);
    if (directory_error) {
        throw std::runtime_error("cannot create directory " + parent.string() + ": " +
                                 directory_error.message());
    }

    fs::path temporary = parent / (path.filename().string() + ".tmp." +
                                   std::to_string(static_cast<long long>(::getpid())) + "." +
                                   std::to_string(counter.fetch_add(1)));
    const mode_t mode = mode_from_permissions(permissions);

    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) {
        throw std::runtime_error(errno_message("cannot open", temporary));
    }

    bool ok = true;
    int saved_errno = 0;
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t result = ::write(fd, data.data() + written, data.size() - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            saved_errno = errno;
            ok = false;
            break;
        }
        written += static_cast<std::size_t>(result);
    }

    if (ok && ::fsync(fd) != 0) {
        saved_errno = errno;
        ok = false;
    }
    if (::close(fd) != 0 && ok) {
        saved_errno = errno;
        ok = false;
    }

    if (!ok) {
        const std::string message = "cannot write " + temporary.string() + ": " +
                                    std::strerror(saved_errno);
        ::unlink(temporary.c_str());
        throw std::runtime_error(message);
    }

    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        const std::string message = errno_message("cannot replace", path);
        ::unlink(temporary.c_str());
        throw std::runtime_error(message);
    }

    const int directory_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd >= 0) {
        ::fsync(directory_fd);
        ::close(directory_fd);
    }
}

}  // namespace st
