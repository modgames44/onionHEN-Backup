/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress payloads / auto-start */
#include "onpress.hpp"
#include "shellui_payload_state.hpp"
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

void *load_payload_thread(void *args);

static OnPressResult prefix_id_payload(OnPressContext &ctx) {
  /* Only dynamic entries id_payload_<n> — not the link id_payloads. */
  if (ctx.id.rfind("id_payload_", 0) != 0) {
    return OnPressResult::NotMine;
  }
  if (g_ui.payloads_list.empty()) {
    return OnPressResult::Handled;
  }
  for (auto entry : g_ui.payloads_list) {
    if (entry.id != ctx.id) {
      continue;
    }
    char pbuf[256];
    int pid = (int)shellui_payload_resolve_recorded_pid(entry.tid.c_str(), pbuf,
                                                        sizeof(pbuf));
    if (pid > 1 && atol(ctx.value.c_str()) == 0) {
      LOG_DEBUG("killing recorded payload pid: %d (%s)", pid,
                  entry.tid.c_str());
      IPC_Client::getInstance(false).ForceKillPID(pid);
      unlink(pbuf);
      notify("notify.process.killed", entry.tid.c_str());
      break;
    } else if (pid <= 1 && atol(ctx.value.c_str()) == 1) {
      pthread_t thr;
      LOG_DEBUG("Payload %s not running", entry.tid.c_str());
      auto info = new PayloadEntry(entry);
      pthread_create(&thr, nullptr, load_payload_thread, (void *)info);
    }
  }
  return OnPressResult::Handled;
}

static OnPressResult prefix_id_auto_payload(OnPressContext &ctx) {
  /* Only dynamic entries id_auto_payload_<n> — not a bare list title. */
  if (ctx.id.rfind("id_auto_payload_", 0) != 0) {
    return OnPressResult::NotMine;
  }
  if (g_ui.auto_payloads_list.empty()) {
    return OnPressResult::Handled;
  }
  for (auto entry : g_ui.auto_payloads_list) {
    if (entry.id != ctx.id) {
      continue;
    }
    std::string auto_path = entry.shellui_path + ".auto_start";
    LOG_DEBUG("Auto start path: %s", auto_path.c_str());
    if (if_exists(auto_path.c_str()) && !atol(ctx.value.c_str())) {
      unlink(auto_path.c_str());
    } else if (atol(ctx.value.c_str())) {
      int fd = open(auto_path.c_str(), O_CREAT | O_RDWR, 0777);
      if (fd < 0) {
        notify("notify.payload.autostart_file");
      } else {
        close(fd);
      }
    }
  }
  return OnPressResult::Handled;
}

static const OnPressPrefixEntry kPrefix[] = {
    {"id_auto_payload_", prefix_id_auto_payload},
    {"id_payload_", prefix_id_payload},
};

const OnPressPrefixEntry *onpress_payloads_prefix(size_t *count) {
  *count = sizeof(kPrefix) / sizeof(kPrefix[0]);
  return kPrefix;
}
