#pragma once

#include <string>

#include "cheats/cheat_engine.h"
#include "util_platform.h"

namespace onion::cheats {

/**
 * Applies / restores cheat patches against a live process.
 * Owns master-code dependency fixups and write verification.
 */
class CheatApplier {
public:
  /**
   * Toggle cheat at index. Returns 0 on success; status holds human message.
   */
  int toggle(const game_context_t &game, onion_cheat_file_t &file, int index,
             std::string &status);
};

} // namespace onion::cheats
