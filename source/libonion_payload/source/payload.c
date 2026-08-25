/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Strict user Payload ELF loader (PID files + private :9020 elfldr).
 */

#include <onion/payload.h>

#include <elfldr_remote.h>
#include <onion/log.h>
#include <onion/notify.h>
#include <onion/proc_query.h>
#include <onion/system_tmp.h>

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern int sceKernelGetProcessName(int pid, char *name);

bool onion_payload_is_elf(const void *buf, size_t size) {
  if (!buf || size < 4)
    return false;
  static const unsigned char kMagic[] = {0x7F, 'E', 'L', 'F'};
  return memcmp(buf, kMagic, 4) == 0;
}

bool onion_payload_elf_key_from_name(const char *name, char *out, size_t out_sz) {
  if (!name || !out || out_sz < 2)
    return false;

  const char *base = strrchr(name, '/');
  base = base ? base + 1 : name;
  if (!base[0] || strcmp(base, ".") == 0 || strcmp(base, "..") == 0)
    return false;

  size_t n = strlen(base);
  if (n >= 4 && strcmp(base + n - 4, ".elf") == 0)
    n -= 4;
  if (n == 0)
    return false; /* bare ".elf" */
  if (n >= out_sz)
    n = out_sz - 1;

  memcpy(out, base, n);
  out[n] = '\0';
  if (out[0] == '\0' || strchr(out, '/') != NULL || strchr(out, '\\') != NULL)
    return false;
  return true;
}

void onion_payload_pid_path(char *out, size_t out_sz, const char *title_id) {
  (void)onion_system_tmp_pid_path(out, out_sz, title_id);
}

pid_t onion_payload_read_pid_file(const char *pid_path) {
  const int f = open(pid_path, O_RDONLY);
  if (f < 0)
    return -1;
  char t[32];
  const int r = (int)read(f, t, sizeof(t) - 1);
  close(f);
  if (r <= 0)
    return -1;
  t[r] = '\0';
  return (pid_t)atoi(t);
}

void onion_payload_write_pid_file(const char *pid_path, pid_t pid) {
  if (!pid_path) {
    return;
  }
  if (pid <= 1) {
    unlink(pid_path);
    return;
  }

  mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
  mkdir(ONION_SYSTEM_TMP_PID_ROOT, 0777);
  const int f = open(pid_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (f < 0)
    return;
  char t[32];
  const int len = snprintf(t, sizeof(t), "%d", pid);
  (void)write(f, t, (size_t)len);
  close(f);
}

/*
 * elfldr process-name rules (see elfldr_remote.h):
 *   - raw bytes over the socket → often "payload.elf"
 *   - file:/path URI      → basename (e.g. web-file-mgr-v0.8.elf)
 * Orbis ki_comm is only COMMLEN (19) chars — long basenames truncate.
 *
 * Kill path must prefer the PID recorded at launch (PID file). Name-only
 * lookup is ambiguous for "payload.elf" when several homebrews run.
 */

#ifndef COMMLEN
#define COMMLEN 19
#endif

/**
 * Name-based resolve for kill fallback only.
 * Prefer title-specific names; avoid bare "payload.elf" (ambiguous).
 */
static pid_t onion_payload_resolve_pid_by_title(const char *title_id) {
  if (!title_id || !title_id[0])
    return -1;

  char nbuf[64];
  char trunc[COMMLEN + 1];
  snprintf(nbuf, sizeof(nbuf), "%s.elf", title_id);

  pid_t pid = find_pid(nbuf);
  if (pid <= 1 && strlen(nbuf) > (size_t)COMMLEN) {
    memcpy(trunc, nbuf, COMMLEN);
    trunc[COMMLEN] = '\0';
    pid = find_pid(trunc);
  }
  if (pid <= 1)
    pid = find_pid(title_id);
  if (pid <= 1)
    pid = onion_find_pid_substr(title_id);
  if (pid <= 1)
    return -1;
  return pid;
}

void onion_payload_stop_by_title(const char *title_id) {
  char pid_path[256];
  onion_payload_pid_path(pid_path, sizeof(pid_path), title_id);

  /* Primary: kill the pid we recorded at launch. */
  pid_t pid = onion_payload_read_pid_file(pid_path);
  /* PID 0/1 are never valid payload targets. */
  if (pid <= 1) {
    if (pid == 1) {
      LOG_WARN("Ignoring bogus payload PID 1 for %s", title_id);
      unlink(pid_path);
    }
    pid = -1;
  } else if (!onion_proc_is_alive(pid)) {
    LOG_WARN("Stale payload PID file for %s (pid=%d dead), removing",
                 title_id, (int)pid);
    unlink(pid_path);
    pid = -1;
  } else {
    char name[32];
    if (sceKernelGetProcessName(pid, name) < 0) {
      LOG_WARN("Stale payload PID file detected for %s, removing", title_id);
      unlink(pid_path);
      pid = -1;
    }
  }

  /* Secondary: title-specific process name only (never generic payload.elf). */
  if (pid <= 1)
    pid = onion_payload_resolve_pid_by_title(title_id);

  if (pid > 1) {
    LOG_INFO("killing pid %d (payload: %s)", (int)pid, title_id);
    if (kill(pid, SIGKILL) != 0)
      LOG_ERROR("kill(%d) failed: %s", (int)pid, strerror(errno));
    unlink(pid_path);
  } else {
    LOG_INFO("stop_by_title: no live pid for %s", title_id);
  }
}

pid_t onion_payload_launch_elfldr(const char *title_id, const uint8_t *elf,
                                  size_t elf_sz) {
  if (!title_id || !title_id[0] || !elf || elf_sz < 4) {
    LOG_ERROR("launch_elfldr: invalid args title=%s elf_sz=%zu",
                 title_id ? title_id : "(null)", elf_sz);
    return -1;
  }
  if (strcmp(title_id, ".") == 0 || strcmp(title_id, "..") == 0 ||
      strchr(title_id, '/') != NULL) {
    LOG_INFO("launch_elfldr: rejected title_id=%s", title_id);
    return -1;
  }

  mkdir("/data/OnionHEN", 0777);
  mkdir("/data/OnionHEN/payloads", 0777);

  char epath[256];
  snprintf(epath, sizeof(epath), "/data/OnionHEN/payloads/%s.elf", title_id);
  LOG_INFO("loading payload via elfldr key=%s path=%s", title_id, epath);

  if (!elfldr_remote_onion_available()) {
    LOG_ERROR("  Private elfldr :%u unavailable; strict payload launch failed",
                 ONION_ELFLDR_PORT);
    return -1;
  }

  const pid_t pid =
      elfldr_remote_onion_write_and_launch_get_pid(epath, elf, elf_sz);
  if (pid <= 1) {
    LOG_ERROR("  Private elfldr :%u returned no valid PID for %s (pid=%d)",
                 ONION_ELFLDR_PORT, title_id, (int)pid);
    return -1;
  }

  char pname[32] = {0};
  if (sceKernelGetProcessName(pid, pname) == 0)
    LOG_INFO("  Launched via %u (pid=%d name=%s)", ONION_ELFLDR_PORT,
                 (int)pid, pname);
  else
    LOG_INFO("  Launched via %u (pid=%d)", ONION_ELFLDR_PORT, (int)pid);
  return pid;
}

uint8_t *onion_payload_read_file(const char *path, size_t *out_size) {
  if (!path || !out_size)
    return NULL;

  const int fd = open(path, O_RDONLY);
  if (fd < 0) {
    LOG_ERROR("Failed to open file %s (%s)", path, strerror(errno));
    return NULL;
  }

  struct stat st;
  if (fstat(fd, &st) != 0) {
    LOG_ERROR("Failed to stat file %s", path);
    close(fd);
    return NULL;
  }
  if (st.st_size <= 0) {
    LOG_INFO("Empty payload file %s", path);
    close(fd);
    return NULL;
  }

  uint8_t *buf = (uint8_t *)malloc((size_t)st.st_size);
  if (!buf) {
    LOG_ERROR("Failed to allocate %lld bytes for payload",
                 (long long)st.st_size);
    close(fd);
    return NULL;
  }

  if (read(fd, buf, (size_t)st.st_size) != st.st_size) {
    LOG_ERROR("Failed to read payload file %s", path);
    free(buf);
    close(fd);
    return NULL;
  }
  close(fd);
  *out_size = (size_t)st.st_size;
  return buf;
}

bool onion_payload_load(const char *path, const char *filename) {
  size_t size = 0;
  uint8_t *buf = onion_payload_read_file(path, &size);
  if (!buf)
    return false;

  char name_buf[256];
  const char *base = filename;
  if (!base || !base[0]) {
    snprintf(name_buf, sizeof(name_buf), "%s", path);
    base = basename(name_buf);
  }

  const size_t base_len = strlen(base);
  if (!(base_len > 4 && strcmp(base + base_len - 4, ".elf") == 0)) {
    LOG_INFO("Not a .elf payload: %s", base);
    onion_notify(1, "notify.payload.elf_only", base);
    free(buf);
    return false;
  }

  if (!onion_payload_is_elf(buf, size)) {
    LOG_ERROR("Invalid ELF file: %s", base);
    onion_notify(1, "notify.payload.invalid_elf", base);
    free(buf);
    return false;
  }

  char key[64];
  if (!onion_payload_elf_key_from_name(base, key, sizeof(key))) {
    LOG_ERROR("Invalid ELF basename (empty stem): %s", base);
    onion_notify(1, "notify.payload.invalid_name", base);
    free(buf);
    return false;
  }

  LOG_INFO("payload launch key=%s (from %s)", key, base);
  char pid_path[256];
  onion_payload_pid_path(pid_path, sizeof(pid_path), key);

  /* Do not tear down a running instance when its replacement cannot even be
   * submitted. The launch function repeats this check to keep its public API
   * strict when called directly. */
  if (!elfldr_remote_onion_available()) {
    LOG_WARN("Private elfldr :%u unavailable; payload %s not replaced",
                 ONION_ELFLDR_PORT, key);
    free(buf);
    return false;
  }

  onion_payload_stop_by_title(key);
  const pid_t pid = onion_payload_launch_elfldr(key, buf, size);
  free(buf);
  /* Only persist real pids; never write 0/1 (PID 1 would hit system init). */
  if (pid > 1)
    onion_payload_write_pid_file(pid_path, pid);
  else
    onion_payload_write_pid_file(pid_path, -1);
  return pid > 1;
}
