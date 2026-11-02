#include "test_util.h"

#include "core/context_budget.h"
#include "core/prompt.h"

#include <string>
#include <vector>

TEST(budget_estimates_tokens) {
    CHECK_EQ(st::estimate_tokens(""), 0);
    CHECK_EQ(st::estimate_tokens("abcd"), 1);
    CHECK_EQ(st::estimate_tokens("abcde"), 2);
    CHECK_EQ(st::estimate_tokens("éééé"), 1);
    CHECK_EQ(st::estimate_tokens(std::string("\xF0\x9F\x98\x80", 4)), 1);

    st::PromptMessage message{"user", "abcd"};
    CHECK_EQ(st::estimate_message_tokens(message), 5);

    const std::vector<st::PromptMessage> messages{{"system", "abcd"}, {"user", "abcde"}};
    CHECK_EQ(st::estimate_prompt_tokens(messages), 5 + 6);
}

TEST(budget_keeps_all_when_it_fits) {
    const std::vector<st::PromptMessage> system{{"system", "short"}};
    const std::vector<st::PromptMessage> history{
        {"assistant", "hello"}, {"user", "hi"}, {"assistant", "there"}};
    const auto result = st::fit_history(system, history, 4096, 300);
    CHECK_EQ(result.history.size(), std::size_t(3));
    CHECK(!result.trimmed);
    CHECK(result.estimated_tokens <= 4096 - 300);
    CHECK_EQ(result.history[0].content, "hello");
    CHECK_EQ(result.history[1].content, "hi");
    CHECK_EQ(result.history[2].content, "there");
}

TEST(budget_trims_oldest_and_preserves_order) {
    const std::vector<st::PromptMessage> system{{"system", "s"}};
    std::vector<st::PromptMessage> history;
    for (int i = 0; i < 10; ++i) {
        history.push_back({"user", std::string(40, 'a')});
    }
    const auto result = st::fit_history(system, history, 80, 20);
    CHECK(result.trimmed);
    CHECK(result.history.size() < history.size());
    CHECK(!result.history.empty());
    CHECK(result.estimated_tokens <= 60);
    for (std::size_t i = 1; i < result.history.size(); ++i) {
        CHECK_EQ(result.history[i].content,
                 history[history.size() - (result.history.size() - i)].content);
    }
}

TEST(budget_keeps_greeting_when_possible) {
    const std::vector<st::PromptMessage> system{{"system", "s"}};
    const std::vector<st::PromptMessage> history{
        {"assistant", "greeting"}, {"user", std::string(40, 'a')},
        {"assistant", std::string(40, 'b')}};
    const auto result = st::fit_history(system, history, 40, 10);
    CHECK(!result.history.empty());
    CHECK_EQ(result.history.front().role, "assistant");
    CHECK_EQ(result.history.front().content, "greeting");
    CHECK(result.estimated_tokens <= 30);
}

TEST(budget_drops_greeting_when_too_large) {
    const std::vector<st::PromptMessage> system{{"system", "s"}};
    const std::vector<st::PromptMessage> history{{"assistant", std::string(400, 'g')}};
    const auto result = st::fit_history(system, history, 40, 10);
    CHECK(result.history.empty());
    CHECK(result.trimmed);
}

TEST(budget_empty_history) {
    const std::vector<st::PromptMessage> system{{"system", "short"}};
    const auto result = st::fit_history(system, {}, 4096, 300);
    CHECK(result.history.empty());
    CHECK(!result.trimmed);
    CHECK_EQ(result.estimated_tokens, st::estimate_prompt_tokens(system));
}

TEST(budget_negative_budget_drops_everything) {
    const std::vector<st::PromptMessage> system{{"system", std::string(400, 's')}};
    const std::vector<st::PromptMessage> history{{"user", "hi"}};
    const auto result = st::fit_history(system, history, 100, 200);
    CHECK(result.history.empty());
    CHECK(result.trimmed);
}
