#include <onion/log.h>
#include "cheats/cheat_applier.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#include "cheats/i_memory_backend.hpp"
#include "util_platform.h"

extern "C" {
}

namespace onion::cheats {
namespace {

bool containsToken(const char *hay, const char *needle) {
  return hay && needle && std::strstr(hay, needle) != nullptr;
}

uint64_t resolveAddr(const util_module_info_t &mod, const onion_patch_t &patch,
                     bool is_ps2, uint64_t base) {
  if (is_ps2 || patch.absolute) {
    return patch.offset;
  }
  (void)mod;
  return base + patch.offset;
}

void fixMasterCode(const game_context_t &game, onion_cheat_file_t &file,
                   onion_cheat_entry_t &entry, uint64_t base,
                   IMemoryBackend &mem) {
  if (file.master_code_id < 0 || !containsToken(entry.name, "MC") ||
      entry.patch_count != 1 || entry.patches[0].section == 0) {
    return;
  }
  auto &master = file.cheats[file.master_code_id];
  if (master.patch_count == 0) {
    return;
  }
  auto &mp = master.patches[0];
  auto &dp = entry.patches[0];
  const uint64_t mc_addr =
      mp.absolute ? mp.offset : (base + mp.offset);
  std::vector<uint8_t> buf(mp.on_len);
  if (mem.read(game.pid, mc_addr, buf.data(), mp.on_len) < 0) {
    return;
  }
  for (size_t i = 0; i + dp.off_len <= mp.on_len; ++i) {
    if (std::memcmp(buf.data() + i, dp.off, dp.off_len) == 0) {
      dp.offset = mp.offset + i;
      return;
    }
  }
  dp.offset = ((mp.offset >> 8) << 8) | (dp.offset & 0xff);
}

} // namespace

int CheatApplier::toggle(const game_context_t &game, onion_cheat_file_t &file,
                         int index, std::string &status) {
  status.clear();
  if (index < 0 || static_cast<size_t>(index) >= file.cheat_count) {
    status = "invalid cheat index";
    return -1;
  }

  auto &entry = file.cheats[index];
  const uint32_t fw = util_system_fw_major();
  if (fw == 0) {
    status = std::string(entry.name) + " -> firmware version unavailable";
    return -1;
  }

  auto backend = MemoryBackendFactory::create(fw);
  game_context_t target = game;
  pid_t pid = game.pid;

  if (file.last_applied_pid != 0 && file.last_applied_pid != pid) {
    LOG_INFO("[Cheat] PID changed %d -> %d, reset states",
                 (int)file.last_applied_pid, (int)pid);
    for (size_t i = 0; i < file.cheat_count; ++i) {
      file.cheats[i].enabled = false;
    }
    file.master_code_id = -1;
  }

  if (entry.module_name[0] == '\0' && game.process_name[0] != '\0') {
    std::snprintf(entry.module_name, sizeof(entry.module_name), "%s",
                  game.process_name);
  }

  util_module_info_t mod{};
  util_module_info_t ps2{};
  if (util_find_module(pid, entry.module_name, &mod) < 0) {
    pid_t fb = -1;
    if (util_find_module_in_app(game.appid, entry.module_name, &fb, &mod) < 0) {
      status = std::string("module not found: ") + entry.module_name;
      return -1;
    }
    pid = fb;
    target.pid = fb;
  }

  const bool is_ps2 =
      util_find_module(pid, "libScePs2EmuMenuDialog.sprx", &ps2) == 0;
  const uint64_t base = mod.sections[0].vaddr;

  if (file.master_code_id < 0 &&
      (containsToken(entry.name, "Master Code") ||
       containsToken(entry.name, "Mastercode"))) {
    file.master_code_id = index;
  } else {
    fixMasterCode(target, file, entry, base, *backend);
  }

  LOG_INFO("[Cheat] Toggle '%s' patches=%zu base=0x%llx %s", entry.name,
               entry.patch_count, (unsigned long long)base,
               entry.enabled ? "ON->OFF" : "OFF->ON");

  for (size_t i = 0; i < entry.patch_count; ++i) {
    auto &patch = entry.patches[i];
    const uint64_t addr = resolveAddr(mod, patch, is_ps2, base);
    const uint8_t *data = entry.enabled ? patch.off : patch.on;
    const size_t len = entry.enabled ? patch.off_len : patch.on_len;

    if (len == 0 || len > ONION_MAX_PATCH_BYTES) {
      status = std::string(entry.name) + " -> invalid patch size";
      return -1;
    }
    if (patch.is_asm) {
      status = std::string(entry.name) + " -> ASM text not assembled";
      return -1;
    }

    if (backend->write(pid, addr, data, len) < 0) {
      status = std::string(entry.name) + " -> write failed";
      return -1;
    }

    if (!entry.enabled) {
      uint8_t verify[ONION_MAX_PATCH_BYTES];
      bool ok = backend->read(pid, addr, verify, len) >= 0 &&
                std::memcmp(verify, data, len) == 0;
      if (!ok && !patch.code_cave_reloc) {
        patch.code_cave_reloc = true;
        if (backend->mapCodeCave(pid, addr, len) == 0 &&
            backend->write(pid, addr, data, len) >= 0 &&
            backend->read(pid, addr, verify, len) >= 0 &&
            std::memcmp(verify, data, len) == 0) {
          ok = true;
        }
      }
      if (!ok) {
        status = std::string(entry.name) + " -> verify mismatch";
        return -1;
      }
    }
  }

  entry.enabled = !entry.enabled;
  file.last_applied_pid = pid;
  status = std::string(entry.name) + (entry.enabled ? " -> enabled" : " -> disabled");
  LOG_INFO("[Cheat] %s", status.c_str());
  return 0;
}

} // namespace onion::cheats
