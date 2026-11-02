#include "core/context_budget.h"

#include <algorithm>
#include <cstddef>

namespace st {
namespace {

int remaining_budget(int context_size, int max_tokens, int system_tokens) {
    const int available = context_size - max_tokens - system_tokens;
    return available > 0 ? available : 0;
}

}  // namespace

int estimate_tokens(const std::string& text) {
    int code_points = 0;
    for (const unsigned char value : text) {
        if ((value & 0xC0) != 0x80) {
            ++code_points;
        }
    }
    return (code_points + 3) / 4;
}

int estimate_message_tokens(const PromptMessage& message) {
    return estimate_tokens(message.content) + 4;
}

int estimate_prompt_tokens(const std::vector<PromptMessage>& messages) {
    int total = 0;
    for (const PromptMessage& message : messages) {
        total += estimate_message_tokens(message);
    }
    return total;
}

BudgetResult fit_history(const std::vector<PromptMessage>& system_messages,
                         const std::vector<PromptMessage>& history,
                         int context_size,
                         int max_tokens) {
    BudgetResult result;
    const int system_tokens = estimate_prompt_tokens(system_messages);
    result.estimated_tokens = system_tokens;

    if (history.empty()) {
        return result;
    }

    const int budget = remaining_budget(context_size, max_tokens, system_tokens);
    int remaining = budget;
    std::size_t first_candidate = 0;
    bool greeting_kept = false;

    if (history.front().role == "assistant") {
        const int greeting_cost = estimate_message_tokens(history.front());
        if (greeting_cost <= remaining) {
            greeting_kept = true;
            remaining -= greeting_cost;
        }
        first_candidate = 1;
    }

    std::vector<std::size_t> kept_indices;
    for (std::size_t index = history.size(); index > first_candidate; --index) {
        const std::size_t candidate = index - 1;
        const int cost = estimate_message_tokens(history[candidate]);
        if (cost > remaining) {
            break;
        }
        kept_indices.push_back(candidate);
        remaining -= cost;
    }
    std::sort(kept_indices.begin(), kept_indices.end());

    if (greeting_kept) {
        result.history.push_back(history.front());
    }
    for (const std::size_t index : kept_indices) {
        result.history.push_back(history[index]);
    }

    result.trimmed = result.history.size() != history.size();
    result.estimated_tokens = system_tokens;
    for (const PromptMessage& message : result.history) {
        result.estimated_tokens += estimate_message_tokens(message);
    }
    return result;
}

}  // namespace st
