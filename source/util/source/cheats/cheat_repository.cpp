#include <onion/fs.h>
#include <onion/log.h>
#include "cheats/cheat_repository.hpp"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <strings.h>
#include <string_view>
#include <sys/stat.h>
#include <unordered_map>
#include <unistd.h>

#include "cheats/i_cheat_parser.hpp"
#include "cheats/cheat_engine.h"
#include "cheats/runtime.h"

namespace onion::cheats {

namespace {

struct CompatibilityCacheEntry {
  FileSignature directory;
  std::string path;
};

std::mutex g_compatibility_cache_mutex;
std::unordered_map<std::string, CompatibilityCacheEntry>
    g_compatibility_cache;

bool isEbootProcess(std::string_view process) {
  return (process.size() == 5 &&
          strncasecmp(process.data(), "eboot", 5) == 0) ||
         (process.size() == 9 &&
          strncasecmp(process.data(), "eboot.bin", 9) == 0);
}

int64_t statMtimeNsec(const struct stat &st) {
#if defined(__APPLE__)
  return static_cast<int64_t>(st.st_mtimespec.tv_nsec);
#else
  return static_cast<int64_t>(st.st_mtim.tv_nsec);
#endif
}

int64_t statCtimeNsec(const struct stat &st) {
#if defined(__APPLE__)
  return static_cast<int64_t>(st.st_ctimespec.tv_nsec);
#else
  return static_cast<int64_t>(st.st_ctim.tv_nsec);
#endif
}

void fillSignature(FileSignature &out, const std::string &path,
                   const struct stat &st) {
  out.path = path;
  out.size = static_cast<uint64_t>(st.st_size);
  out.inode = static_cast<uint64_t>(st.st_ino);
  out.mtime = static_cast<int64_t>(st.st_mtime);
  out.mtime_nsec = statMtimeNsec(st);
  out.ctime = static_cast<int64_t>(st.st_ctime);
  out.ctime_nsec = statCtimeNsec(st);
}

bool readDirectorySignature(FileSignature &out) {
  struct stat st {};
  if (::stat(ONION_CHEATS_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
    return false;
  }
  fillSignature(out, ONION_CHEATS_DIR, st);
  return true;
}

std::string findExactPath(const std::string &basename) {
  for (int rank = 0;; ++rank) {
    const char *extension = onion_cheat_extension_for_rank(rank);
    if (extension == nullptr) {
      return {};
    }
    const std::string path = std::string(ONION_CHEATS_DIR) + "/" + basename +
                             "." + extension;
    if (CheatRepository::fileExists(path)) {
      return path;
    }
  }
}

bool isRegularEntry(const char *directory, const struct dirent *entry) {
  if (directory == nullptr || entry == nullptr) {
    return false;
  }
#ifdef DT_DIR
  if (entry->d_type == DT_DIR) {
    return false;
  }
#endif
#ifdef DT_REG
  if (entry->d_type == DT_REG) {
    return true;
  }
#endif

  char path[512];
  const int written =
      std::snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(path)) {
    return false;
  }
  struct stat st {};
  return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

struct CompatibilityCandidate {
  int extension_rank = -1;
  std::string name;
};

std::string scanCompatibilityPath(const game_context_t &game,
                                   const std::string &version,
                                   std::string_view process) {
  if (!process.empty() && !isEbootProcess(process)) {
    return {};
  }

  DIR *directory = ::opendir(ONION_CHEATS_DIR);
  if (directory == nullptr) {
    return {};
  }

  CompatibilityCandidate best;
  size_t best_count = 0;
  while (const struct dirent *entry = ::readdir(directory)) {
    onion_cheat_filename_t parts{};
    if (onion_cheat_parse_filename(entry->d_name, &parts) < 0 ||
        strcasecmp(parts.title_id, game.title_id) != 0 ||
        std::strcmp(parts.version, version.c_str()) != 0 ||
        !onion_cheat_is_legacy_eboot_alias(parts.suffix) ||
        !isRegularEntry(ONION_CHEATS_DIR, entry)) {
      continue;
    }

    if (best.extension_rank < 0 ||
        parts.extension_rank < best.extension_rank) {
      best = {parts.extension_rank, entry->d_name};
      best_count = 1;
    } else if (parts.extension_rank == best.extension_rank) {
      ++best_count;
    }
  }
  ::closedir(directory);

  if (best.extension_rank < 0) {
    return {};
  }
  if (best_count > 1) {
    LOG_WARN(
        "[repository] ambiguous compatibility cheats for %s %s (%zu candidates)",
        game.title_id, version.c_str(), best_count);
    return {};
  }

  const std::string path =
      std::string(ONION_CHEATS_DIR) + "/" + best.name;
  LOG_WARN("[repository] using compatibility cheat alias %s",
           path.c_str());
  return path;
}

std::string resolveCompatibilityPath(const game_context_t &game,
                                     const std::string &version,
                                     std::string_view process) {
  FileSignature directory;
  if (!readDirectorySignature(directory)) {
    return {};
  }

  const std::string key = std::string(game.title_id) + "\n" + version +
                          "\n" + std::string(process);
  bool cache_hit = false;
  std::string cached_path;
  {
    std::lock_guard<std::mutex> lock(g_compatibility_cache_mutex);
    const auto it = g_compatibility_cache.find(key);
    if (it != g_compatibility_cache.end() &&
        it->second.directory.sameIdentity(directory)) {
      cache_hit = true;
      cached_path = it->second.path;
    }
  }
  if (cache_hit &&
      (cached_path.empty() || CheatRepository::fileExists(cached_path))) {
    return cached_path;
  }

  const std::string path = scanCompatibilityPath(game, version, process);
  {
    std::lock_guard<std::mutex> lock(g_compatibility_cache_mutex);
    g_compatibility_cache[key] = {directory, path};
  }
  return path;
}

} // namespace

std::string CheatRepository::resolvePath(const game_context_t &game) {
  char version[32];
  char process[sizeof(game.process_name)];

  if (game.title_id[0] == '\0' || game.version[0] == '\0' ||
      std::strcmp(game.version, "unknown") == 0) {
    return {};
  }
  onion_cheat_normalize_filename_token(game.version, version, sizeof(version));
  onion_cheat_normalize_filename_token(game.process_name, process,
                                       sizeof(process));

  const std::string basename =
      std::string(game.title_id) + "_" + version;
  if (const std::string path = findExactPath(basename); !path.empty()) {
    return path;
  }

  if (process[0] != '\0') {
    if (const std::string path = findExactPath(basename + "_" + process);
        !path.empty()) {
      return path;
    }
  }

  return resolveCompatibilityPath(game, version, process);
}

bool CheatRepository::fileExists(const std::string &path) {
  return !path.empty() && if_exists(path.c_str());
}

bool CheatRepository::statSignature(const std::string &path,
                                    FileSignature &out) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    return false;
  }
  fillSignature(out, path, st);
  return true;
}

int CheatRepository::loadFile(const std::string &path, onion_cheat_file_t &out) {
  return CheatParserFactory::loadFile(path, out);
}

void CheatRepository::ensureCheatsDir() {
  ::mkdir(ONION_DATA_ROOT, 0777);
  ::mkdir(ONION_CHEATS_DIR, 0777);
}

int CheatRepository::flattenInstallTree(const std::string &root) {
  return onion_cheat_flatten_install_tree(root.c_str());
}

} // namespace onion::cheats
