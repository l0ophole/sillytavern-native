#pragma once

#include "core/character_store.h"

#include <string>
#include <vector>

namespace st {

std::vector<CharacterEntry> filter_characters(const std::vector<CharacterEntry>& entries,
                                              const std::string& query);

}  // namespace st
