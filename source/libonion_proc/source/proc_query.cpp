/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * sysctl-based process lookup (single implementation for all OnionHEN bins).
 */

extern "C" {
#include <onion/proc_query.h>
}

#include "freebsd-helper.h"

#include <cstdlib>
#include <cstring>
#include <sys/sysctl.h>
#include <unistd.h>

namespace {

onion_get_process_name_fn g_get_name = nullptr;
onion_get_app_info_fn g_get_app_info = nullptr;
onion_get_bigapp_id_fn g_get_bigapp = nullptr;

// app_info_t starts with uint32_t app_id (see shellui external_symbols).

pid_t scan_kinfo(const char *name, bool substr) {
  if (!name || !*name) {
    return -1;
  }

  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t buf_size = 0;
  if (sysctl(mib, 4, nullptr, &buf_size, nullptr, 0)) {
    return -1;
  }
  void *buf = malloc(buf_size);
  if (!buf) {
    return -1;
  }
  if (sysctl(mib, 4, buf, &buf_size, nullptr, 0)) {
    free(buf);
    return -1;
  }

  pid_t found = -1;
  for (char *ptr = static_cast<char *>(buf);
       ptr < static_cast<char *>(buf) + buf_size;) {
    auto *ki = reinterpret_cast<struct kinfo_proc *>(ptr);
    if (ki->ki_structsize <= 0) {
      break;
    }
    ptr += ki->ki_structsize;

    const char *comm = ki->ki_comm;
    bool match = false;
    if (substr) {
      match = (comm && strstr(comm, name) != nullptr);
    } else {
      match = (comm && strcmp(comm, name) == 0);
    }
    // Also match thread name when present (daemon historically used ki_tdname).
    if (!match && ki->ki_tdname[0]) {
      if (substr) {
        match = strstr(ki->ki_tdname, name) != nullptr;
      } else {
        match = strcmp(ki->ki_tdname, name) == 0;
      }
    }
    if (match) {
      found = ki->ki_pid;
      break;
    }
  }

  free(buf);
  return found;
}

} // namespace

extern "C" void onion_proc_set_sce_hooks(onion_get_process_name_fn get_name,
                                         onion_get_app_info_fn get_app_info,
                                         onion_get_bigapp_id_fn get_bigapp) {
  g_get_name = get_name;
  g_get_app_info = get_app_info;
  g_get_bigapp = get_bigapp;
}

extern "C" pid_t onion_find_pid(const char *name) {
  return scan_kinfo(name, /*substr=*/false);
}

extern "C" pid_t find_pid(const char *name) {
  return onion_find_pid(name);
}

extern "C" pid_t onion_find_pid_substr(const char *substr) {
  return scan_kinfo(substr, /*substr=*/true);
}

extern "C" bool onion_proc_is_alive(pid_t pid) {
  if (pid <= 0) {
    return false;
  }
  int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>(pid)};
  return sysctl(mib, 4, nullptr, nullptr, nullptr, 0) == 0;
}

extern "C" pid_t onion_find_pid_ex(const char *name, bool needle, bool for_bigapp,
                                   bool need_eboot) {
  if (!name) {
    return -1;
  }

  // Fast path: no big-app filter — name match only.
  if (!for_bigapp) {
    return needle ? onion_find_pid_substr(name) : onion_find_pid(name);
  }

  // Big-app path needs SCE hooks installed by the host binary (shellui).
  if (!g_get_bigapp) {
    return needle ? onion_find_pid_substr(name) : onion_find_pid(name);
  }

  int bigappid = g_get_bigapp();
  if (bigappid < 0) {
    return -1;
  }

  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t buf_size = 0;
  if (sysctl(mib, 4, nullptr, &buf_size, nullptr, 0)) {
    return -1;
  }
  void *buf = malloc(buf_size);
  if (!buf) {
    return -1;
  }
  if (sysctl(mib, 4, buf, &buf_size, nullptr, 0)) {
    free(buf);
    return -1;
  }

  pid_t found = -1;
  char proc_name[64] = {0};

  for (char *ptr = static_cast<char *>(buf);
       ptr < static_cast<char *>(buf) + buf_size;) {
    auto *ki = reinterpret_cast<struct kinfo_proc *>(ptr);
    if (ki->ki_structsize <= 0) {
      break;
    }
    ptr += ki->ki_structsize;

    char appinfo_buf[128]{};
    int32_t app_id = 0;
    if (g_get_app_info) {
      if (g_get_app_info(ki->ki_pid, appinfo_buf) == 0) {
        app_id = *reinterpret_cast<int32_t *>(appinfo_buf);
      }
    }

    if (g_get_name) {
      if (g_get_name(ki->ki_pid, proc_name) != 0) {
        continue;
      }
    } else {
      strncpy(proc_name, ki->ki_comm, sizeof(proc_name) - 1);
    }

    bool success = false;
    if (need_eboot) {
      success = (bigappid == app_id) && (strcmp(ki->ki_comm, proc_name) == 0);
    } else {
      success = (bigappid == app_id);
    }
    // Optional name filter when not using eboot matching alone.
    if (!success && !need_eboot && name[0] && bigappid == app_id) {
      if (needle) {
        success = strstr(proc_name, name) != nullptr;
      } else {
        success = strcmp(proc_name, name) == 0;
      }
    }

    if (success) {
      found = ki->ki_pid;
      break;
    }
  }

  free(buf);
  return found;
}
