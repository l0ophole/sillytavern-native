#include "test_util.h"
#include "temp_dir.h"

#include "core/atomic_file.h"
#include "core/log.h"

#include <string>

TEST(redact_replaces_every_secret) {
    const auto text = st::redact_secrets("key=sk-abc other=sk-abc", {"sk-abc"});
    CHECK_EQ(text, "key=[REDACTED] other=[REDACTED]");
}

TEST(redact_ignores_empty_secret) {
    CHECK_EQ(st::redact_secrets("plain", {""}), "plain");
}

TEST(log_writes_to_file_and_redacts) {
    TempDir temp;
    const auto file = temp.path() / "app.log";
    st::log_init(file, false);
    st::log_set_redactions({"sk-secret"});
    st::log_write(st::LogLevel::Info, "api key sk-secret");
    st::log_shutdown();
    const auto contents = st::read_file(file);
    CHECK(contents.find("api key") != std::string::npos);
    CHECK(contents.find("sk-secret") == std::string::npos);
    CHECK(contents.find("[REDACTED]") != std::string::npos);
}
