#include "test_util.h"
#include "temp_dir.h"

#include "core/file_lock.h"

#include <csignal>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

TEST(file_lock_blocks_second_holder) {
    TempDir temp;
    const auto path = temp.path() / "instance.lock";
    auto first = st::FileLock::try_acquire(path);
    CHECK(first.has_value());
    CHECK(!st::FileLock::try_acquire(path).has_value());
    first.reset();
    CHECK(st::FileLock::try_acquire(path).has_value());
}

TEST(file_lock_is_released_when_holder_process_exits) {
    TempDir temp;
    const auto path = temp.path() / "instance.lock";
    int pipefd[2] = {-1, -1};
    CHECK_EQ(::pipe(pipefd), 0);
    const pid_t child = ::fork();
    CHECK(child >= 0);
    if (child == 0) {
        ::close(pipefd[0]);
        auto lock = st::FileLock::try_acquire(path);
        const char state = lock.has_value() ? '1' : '0';
        (void)!::write(pipefd[1], &state, 1);
        ::close(pipefd[1]);
        for (;;) ::pause();
    }
    ::close(pipefd[1]);
    char state = 0;
    CHECK_EQ(::read(pipefd[0], &state, 1), 1);
    ::close(pipefd[0]);
    CHECK_EQ(state, '1');
    CHECK(!st::FileLock::try_acquire(path).has_value());
    ::kill(child, SIGKILL);
    CHECK_EQ(::waitpid(child, nullptr, 0), child);
    CHECK(st::FileLock::try_acquire(path).has_value());
}

TEST(file_lock_records_holder_pid) {
    TempDir temp;
    const auto path = temp.path() / "instance.lock";
    auto lock = st::FileLock::try_acquire(path);
    CHECK(lock.has_value());
    const auto pid = st::FileLock::holder_pid(path);
    CHECK(pid.has_value());
    CHECK_EQ(pid.value(), static_cast<long>(::getpid()));
}

TEST(file_lock_missing_file_has_no_holder_pid) {
    TempDir temp;
    CHECK(!st::FileLock::holder_pid(temp.path() / "missing.lock").has_value());
}
