#pragma once

#include "core/prompt.h"

#include <string>
#include <vector>

namespace st {

int estimate_tokens(const std::string& text);
int estimate_message_tokens(const PromptMessage& message);
int estimate_prompt_tokens(const std::vector<PromptMessage>& messages);

struct BudgetResult {
    std::vector<PromptMessage> history;
    int estimated_tokens = 0;
    bool trimmed = false;
};

BudgetResult fit_history(const std::vector<PromptMessage>& system_messages,
                         const std::vector<PromptMessage>& history,
                         int context_size,
                         int max_tokens);

}  // namespace st
