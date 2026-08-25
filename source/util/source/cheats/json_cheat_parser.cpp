#include "cheats/i_cheat_parser.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "cheats/cheat_engine_internal.h"

namespace onion::cheats {
namespace {

int parseMemoryObject(const char *start, const char *end, onion_patch_t *patch) {
  char value[512];

  std::memset(patch, 0, sizeof(*patch));
  patch->section = 0;
  patch->absolute = false;

  if (onion_cheat_extract_scalar(start, end, "offset", value, sizeof(value)) <
      0) {
    return -1;
  }
  patch->offset = std::strtoull(value, nullptr, 16);

  if (onion_cheat_extract_string(start, end, "on", value, sizeof(value)) < 0 ||
      onion_cheat_hex_decode(value, patch->on, sizeof(patch->on),
                             &patch->on_len) < 0) {
    return -1;
  }
  if (onion_cheat_extract_string(start, end, "off", value, sizeof(value)) < 0 ||
      onion_cheat_hex_decode(value, patch->off, sizeof(patch->off),
                             &patch->off_len) < 0) {
    return -1;
  }

  if (onion_cheat_extract_scalar(start, end, "section", value,
                                 sizeof(value)) == 0) {
    const int section = std::atoi(value);
    if (section < MODULE_INFO_MAX_SECTIONS) {
      patch->section = section;
    }
  }
  if (onion_cheat_extract_scalar(start, end, "absolute", value,
                                 sizeof(value)) == 0) {
    patch->absolute = (std::strcmp(value, "1") == 0 ||
                       std::strcmp(value, "true") == 0 ||
                       std::strcmp(value, "\"1\"") == 0);
  }

  return 0;
}

int parseModObject(const char *start, const char *end, const char *process_name,
                   onion_cheat_entry_t *entry) {
  const char *memory = onion_cheat_find_key(start, end, "memory");
  const char *arr_end = nullptr;
  const char *p = nullptr;

  std::memset(entry, 0, sizeof(*entry));
  if (onion_cheat_extract_string(start, end, "name", entry->name,
                                 sizeof(entry->name)) < 0) {
    return -1;
  }
  onion_cheat_extract_string(start, end, "description", entry->description,
                             sizeof(entry->description));
  std::snprintf(entry->module_name, sizeof(entry->module_name), "%s",
                process_name);

  if (memory == nullptr) {
    return 0;
  }
  memory = onion_cheat_skip_ws(memory, end);
  if (memory >= end || *memory != '[') {
    return -1;
  }

  arr_end = onion_cheat_find_matching(memory, end, '[', ']');
  if (arr_end == nullptr) {
    return -1;
  }

  p = memory + 1;
  while (p < arr_end) {
    if (*p == '{') {
      const char *obj_end =
          onion_cheat_find_matching(p, arr_end + 1, '{', '}');
      if (obj_end == nullptr) {
        return -1;
      }
      if (onion_cheat_entry_ensure_patch(entry) != 0) {
        return -1;
      }
      if (parseMemoryObject(p, obj_end + 1,
                            &entry->patches[entry->patch_count]) == 0) {
        ++entry->patch_count;
      }
      p = obj_end + 1;
      continue;
    }
    ++p;
  }

  return 0;
}

/** GoldHEN uses "credits"; some files may use "authors". Both are string arrays. */
void parseAuthorArray(const char *json, size_t size, const char *key,
                      onion_cheat_file_t &out) {
  const char *end = json + size;
  const char *arr = onion_cheat_find_key(json, end, key);
  const char *arr_end = nullptr;
  const char *p = nullptr;

  if (arr == nullptr) {
    return;
  }
  arr = onion_cheat_skip_ws(arr, end);
  if (arr >= end || *arr != '[') {
    return;
  }
  arr_end = onion_cheat_find_matching(arr, end, '[', ']');
  if (arr_end == nullptr) {
    return;
  }

  p = arr + 1;
  while (p < arr_end) {
    p = onion_cheat_skip_ws(p, arr_end);
    if (p >= arr_end) {
      break;
    }
    if (*p == '"') {
      const char *q = ++p;
      char name[ONION_AUTHOR_NAME_LEN];
      size_t len = 0;
      while (q < arr_end && !(*q == '"' && q[-1] != '\\')) {
        ++q;
      }
      if (q >= arr_end) {
        break;
      }
      len = static_cast<size_t>(q - p);
      if (len >= sizeof(name)) {
        len = sizeof(name) - 1;
      }
      if (len > 0) {
        std::memcpy(name, p, len);
        name[len] = '\0';
        onion_cheat_file_add_author(&out, name);
      }
      p = q + 1;
      continue;
    }
    ++p;
  }
}

} // namespace

class JsonCheatParser final : public ICheatParser {
public:
  const char *name() const override { return "json"; }

  int parse(const uint8_t *data, size_t size, onion_cheat_file_t &out) override {
    if (data == nullptr || size == 0) {
      return -1;
    }
    const char *json = reinterpret_cast<const char *>(data);
    const char *mods = nullptr;
    const char *mods_end = nullptr;
    const char *p = nullptr;

    onion_cheat_file_clear(&out);

    if (onion_cheat_extract_string(json, json + size, "process", out.process,
                                   sizeof(out.process)) < 0 ||
        onion_cheat_extract_string(json, json + size, "name", out.name,
                                   sizeof(out.name)) < 0) {
      return -1;
    }

    /* Prefer credits (GoldHEN); also accept authors. Dedup via add_author. */
    parseAuthorArray(json, size, "credits", out);
    parseAuthorArray(json, size, "authors", out);

    mods = onion_cheat_find_key(json, json + size, "mods");
    if (mods == nullptr) {
      return -1;
    }

    mods = onion_cheat_skip_ws(mods, json + size);
    if (*mods != '[') {
      return -1;
    }

    mods_end = onion_cheat_find_matching(mods, json + size, '[', ']');
    if (mods_end == nullptr) {
      return -1;
    }

    p = mods + 1;
    while (p < mods_end) {
      if (*p == '{') {
        const char *obj_end =
            onion_cheat_find_matching(p, mods_end + 1, '{', '}');
        if (obj_end == nullptr) {
          break;
        }
        if (onion_cheat_file_ensure_cheat(&out) != 0) {
          break;
        }
        onion_cheat_entry_t *entry = &out.cheats[out.cheat_count];
        if (parseModObject(p, obj_end + 1, out.process, entry) == 0 &&
            entry->patch_count > 0) {
          ++out.cheat_count;
        }
        p = obj_end + 1;
        continue;
      }
      ++p;
    }

    return out.cheat_count > 0 ? 0 : -1;
  }
};

std::unique_ptr<ICheatParser> makeJsonCheatParser() {
  return std::make_unique<JsonCheatParser>();
}

} // namespace onion::cheats
