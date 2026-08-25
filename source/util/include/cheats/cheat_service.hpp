#pragma once

#include <mutex>
#include <string>

#include "cheats/cheat_applier.hpp"
#include "cheats/cheat_repository.hpp"
#include "cheats/cheat_engine.h"
#include "cheats/runtime.h"
#include "util_platform.h"

namespace onion::cheats {

/**
 * Facade for IPC and lifecycle. Thread-safe process-wide service.
 *
 * Patterns: Facade + Singleton (process lifetime) + RAII locking.
 */
class CheatService {
public:
  static CheatService &instance();

  void ensureDir();
  void onGameExec(pid_t pid, const char *title_id, int appid);
  void onGameExit(pid_t pid);

  /** Write ShellUI list JSON to outPath. 0 = ok. */
  int exportList(const std::string &title_id, const std::string &version,
                 int pid, int appid, const std::string &out_path);

  int toggle(int pid, int appid, const std::string &title_id,
             const std::string &version, int index, std::string &status);

  int flattenInstallTree(const std::string &root);

  CheatService(const CheatService &) = delete;
  CheatService &operator=(const CheatService &) = delete;

private:
  CheatService();
  ~CheatService();

  int fillGame(game_context_t &game, const std::string &title_id,
               const std::string &version, int pid, int appid);
  int refreshLocked(const game_context_t &game);
  void disableEnabledLocked(const char *reason);
  void clearFileLocked();
  int writeListJson(const std::string &out_path) const;

  mutable std::mutex mu_;
  bool loaded_ = false;
  bool has_tracked_game_ = false;
  pid_t tracked_pid_ = 0;
  game_context_t game_{};
  onion_cheat_file_t file_{};
  FileSignature sig_{};
  CheatApplier applier_{};
};

} // namespace onion::cheats
