/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Event-driven app jailbreak listener.
 *
 * Protocol (homebrew -> daemon):
 *   1. A process with a whitelisted Title ID starts.
 *   2. The app writes JSON {"PID":"<pid>"} (numeric PID also accepted) to
 *        /mnt/sandbox/<TID>_<000-050>/download0/<name>
 *      Accepted names:
 *        - etahen_jailbreak
 *        - onionhen_jailbreak
 *   3. SceSysCore NOTE_EXEC/NOTE_EXIT events identify app lifetime. Vnode
 *      events then follow sandbox -> slot -> download0 -> request file.
 *
 * There is no periodic foreground-app query or sandbox scan. The only scan of
 * the 51 possible slots happens when an allowed app starts, or when the
 * sandbox root reports that one of its direct children changed.
 */

#include "daemon_ops.hpp"
#include "globalconf.hpp"

#include <onion/app_jailbreak_policy.hpp>
#include <onion/platform.h>
#include <onion/proc_query.h>
#include <onion/settings.hpp>
#include "onion_cjson.hpp"

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct app_info {
  uint32_t app_id;
  uint64_t unknown1;
  char title_id[14];
  char unknown2[0x3c];
} app_info_t;

extern "C" {
#include <ps5/kernel.h>

int sceKernelGetAppInfo(pid_t pid, app_info_t *info);
int sceKernelGetProcessName(int pid, char *name);
}

namespace {

pthread_mutex_t jb_control_lock = PTHREAD_MUTEX_INITIALIZER;
std::atomic_bool jb_listener_enabled{false};
int jb_control_write_fd = -1;

/** Same authid etaHEN / Hijacker::jailbreak historically used. */
constexpr uint64_t kJbAuthId = 0x4801000000000013ull;
constexpr size_t kMaxRequestBytes = 4096;
constexpr int kProcResolveAttempts = 3;
constexpr useconds_t kProcResolveUsleep = 30 * 1000;
constexpr const char *kMountRoot = "/mnt";
constexpr const char *kSandboxRoot = "/mnt/sandbox";
constexpr const char *kJailbreakReqNames[] = {
    "etahen_jailbreak",
    "onionhen_jailbreak",
};

bool is_whitelisted_app(const std::string &tid,
                        const onion::AppJailbreakAllowlist &allowlist) {
  return onion::app_jailbreak::is_whitelisted(tid, allowlist);
}

int jb_read_uid(pid_t pid) {
  if (kernel_get_proc(pid) == 0) {
    return -1;
  }
  return static_cast<int>(kernel_get_ucred_uid(pid));
}

uint64_t jb_read_authid(pid_t pid) {
  if (kernel_get_proc(pid) == 0) {
    return 0;
  }
  return kernel_get_ucred_authid(pid);
}

/**
 * Full app jailbreak (uid0 + authid + caps + optional sandbox escape).
 * Uses ps5-payload-sdk kernel helpers so it tracks the FW symbols the HEN
 * already resolved into KERNEL_ADDRESS_*.
 */
bool jb_apply_privileges(pid_t pid, bool escape_sandbox) {
  static const uint8_t kFullCaps[16] = {
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  };

  const intptr_t kproc = kernel_get_proc(pid);
  if (kproc == 0) {
    LOG_INFO("[JB] kernel_get_proc(%d)=0 (ALLPROC=0x%lx rootvnode=0x%lx)",
             static_cast<int>(pid),
             static_cast<unsigned long>(KERNEL_ADDRESS_ALLPROC),
             static_cast<unsigned long>(KERNEL_ADDRESS_ROOTVNODE));
    return false;
  }

  if (kernel_set_ucred_uid(pid, 0) != 0) {
    LOG_ERROR("[JB] kernel_set_ucred_uid failed pid=%d",
              static_cast<int>(pid));
    return false;
  }
  (void)kernel_set_ucred_ruid(pid, 0);
  (void)kernel_set_ucred_svuid(pid, 0);
  (void)kernel_set_ucred_rgid(pid, 0);

  /* cr_ngroups is not exposed by SDK helpers; its ucred offset is 0x10. */
  const intptr_t ucred = kernel_get_proc_ucred(pid);
  if (ucred) {
    const uint32_t ngroups = 0;
    (void)kernel_copyin(&ngroups, ucred + 0x10, sizeof(ngroups));
  }

  if (kernel_set_ucred_authid(pid, kJbAuthId) != 0) {
    LOG_ERROR("[JB] kernel_set_ucred_authid failed pid=%d",
              static_cast<int>(pid));
    return false;
  }
  if (kernel_set_ucred_caps(pid, kFullCaps) != 0) {
    LOG_ERROR("[JB] kernel_set_ucred_caps failed pid=%d",
              static_cast<int>(pid));
    return false;
  }

  /* sceAttr byte at ucred+0x83 = 0x80 enables ptrace. */
  uint8_t attrs[32] = {};
  if (kernel_get_ucred_attrs(pid, attrs) != 0) {
    LOG_ERROR("[JB] kernel_get_ucred_attrs failed pid=%d",
              static_cast<int>(pid));
    return false;
  }
  attrs[3] = 0x80;
  if (kernel_set_ucred_attrs(pid, attrs) != 0) {
    LOG_ERROR("[JB] kernel_set_ucred_attrs failed pid=%d",
              static_cast<int>(pid));
    return false;
  }

  if (escape_sandbox) {
    const intptr_t root = kernel_get_root_vnode();
    if (root == 0) {
      LOG_ERROR("[JB] kernel_get_root_vnode()=0 - cannot escape sandbox");
      return false;
    }
    if (kernel_set_proc_rootdir(pid, root) != 0) {
      LOG_ERROR("[JB] kernel_set_proc_rootdir failed pid=%d",
                static_cast<int>(pid));
      return false;
    }
    if (kernel_set_proc_jaildir(pid, root) != 0) {
      LOG_ERROR("[JB] kernel_set_proc_jaildir failed pid=%d",
                static_cast<int>(pid));
      return false;
    }
  }

  return kernel_get_ucred_uid(pid) == 0;
}

bool path_is_directory(const std::string &path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool path_is_within(const std::string &path, const std::string &root) {
  return path == root ||
         (path.size() > root.size() &&
          path.compare(0, root.size(), root) == 0 &&
          path[root.size()] == '/');
}

enum class WatchKind {
  MountRoot,
  SandboxRoot,
  SlotDirectory,
  DownloadDirectory,
  RequestFile,
};

const char *watch_kind_name(WatchKind kind) {
  switch (kind) {
  case WatchKind::MountRoot:
    return "mount-root";
  case WatchKind::SandboxRoot:
    return "sandbox-root";
  case WatchKind::SlotDirectory:
    return "slot";
  case WatchKind::DownloadDirectory:
    return "download0";
  case WatchKind::RequestFile:
    return "request";
  }
  return "unknown";
}

struct VnodeWatch {
  int fd = -1;
  WatchKind kind = WatchKind::MountRoot;
  std::string path;
  std::string tid;
  std::string slot_path;
};

struct TrackedApp {
  uint32_t app_id = 0;
  std::set<pid_t> pids;
};

class JailbreakEventLoop {
public:
  explicit JailbreakEventLoop(int control_read_fd)
      : control_read_fd_(control_read_fd) {}

  ~JailbreakEventLoop() {
    close_vnode_watches();
    if (kq_ >= 0) {
      close(kq_);
    }
  }

  void run() {
    if (!rebuild()) {
      return;
    }

    struct kevent events[16];
    while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
      const int count = kevent(kq_, nullptr, 0, events,
                               sizeof(events) / sizeof(events[0]), nullptr);
      if (count < 0) {
        if (errno == EINTR) {
          continue;
        }
        LOG_ERROR("[JB] kevent wait failed: %s", strerror(errno));
        break;
      }

      for (int i = 0; i < count; ++i) {
        const struct kevent &event = events[i];
        if (event.filter == EVFILT_READ &&
            event.ident == static_cast<uintptr_t>(control_read_fd_)) {
          drain_control_pipe();
          if (g_stack_shutting_down.load(std::memory_order_acquire)) {
            return;
          }
          /* Replacing kqueue invalidates every remaining event in this batch. */
          if (!rebuild()) {
            return;
          }
          break;
        }

        if (event.flags & EV_ERROR) {
          LOG_ERROR("[JB] kqueue event error filter=%d ident=%lu error=%lld",
                    static_cast<int>(event.filter),
                    static_cast<unsigned long>(event.ident),
                    static_cast<long long>(event.data));
          continue;
        }

        if (event.filter == EVFILT_PROC) {
          handle_process_event(event);
        } else if (event.filter == EVFILT_VNODE) {
          handle_vnode_event(event);
        }
      }
    }
  }

private:
  bool install_control_watch(int kq) const {
    struct kevent event;
    EV_SET(&event, static_cast<uintptr_t>(control_read_fd_), EVFILT_READ,
           EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);
    return kevent(kq, &event, 1, nullptr, 0, nullptr) == 0;
  }

  bool rebuild() {
    const int new_kq = kqueue();
    if (new_kq < 0) {
      LOG_ERROR("[JB] kqueue create failed: %s", strerror(errno));
      return false;
    }
    if (!install_control_watch(new_kq)) {
      LOG_ERROR("[JB] control-pipe watch failed: %s", strerror(errno));
      close(new_kq);
      return false;
    }

    close_vnode_watches();
    if (kq_ >= 0) {
      close(kq_);
    }
    kq_ = new_kq;
    syscore_pid_ = -1;
    pid_to_tid_.clear();
    active_apps_.clear();
    settings_ = g_settings.snapshot();

    const bool enabled =
        jb_listener_enabled.load(std::memory_order_acquire) &&
        settings_.app_jailbreak_enabled;
    if (!enabled) {
      LOG_INFO("[JB] event listener idle (App jailbreak disabled)");
      return true;
    }

    syscore_pid_ = onion_find_pid("SceSysCore.elf");
    if (syscore_pid_ <= 0) {
      LOG_ERROR("[JB] cannot find SceSysCore.elf; lifecycle listener inactive");
      return true;
    }

    struct kevent event;
    EV_SET(&event, static_cast<uintptr_t>(syscore_pid_), EVFILT_PROC,
           EV_ADD | EV_ENABLE | EV_CLEAR,
           NOTE_FORK | NOTE_EXEC | NOTE_TRACK, 0, nullptr);
    if (kevent(kq_, &event, 1, nullptr, 0, nullptr) != 0) {
      LOG_ERROR("[JB] cannot watch SceSysCore pid=%d: %s",
                static_cast<int>(syscore_pid_), strerror(errno));
      syscore_pid_ = -1;
      return true;
    }

    LOG_INFO("[JB] event listener active: SceSysCore pid=%d, "
             "fflags=NOTE_FORK|NOTE_EXEC|NOTE_TRACK, no polling/fallback",
             static_cast<int>(syscore_pid_));
    return true;
  }

  void drain_control_pipe() const {
    char buffer[64];
    while (read(control_read_fd_, buffer, sizeof(buffer)) > 0) {
    }
  }

  void handle_process_event(const struct kevent &event) {
    const pid_t pid = static_cast<pid_t>(event.ident);

    LOG_INFO("[JB][diag] proc event pid=%d fflags=0x%x data=%lld flags=0x%x",
             static_cast<int>(pid), event.fflags,
             static_cast<long long>(event.data), event.flags);

    if (event.fflags & NOTE_TRACKERR) {
      LOG_ERROR("[JB][diag] NOTE_TRACKERR pid=%d parent=%lld; no fallback",
                static_cast<int>(pid), static_cast<long long>(event.data));
    }
    if (event.fflags & NOTE_EXEC) {
      inspect_app_process(pid, "exec");
    }
    if (event.fflags & NOTE_EXIT) {
      if (pid == syscore_pid_) {
        LOG_ERROR("[JB] SceSysCore exited; lifecycle listener inactive");
        syscore_pid_ = -1;
      } else {
        remove_app_process(pid);
      }
    }
  }

  void inspect_app_process(pid_t pid, const char *source) {
    if (pid <= 1 || pid_to_tid_.find(pid) != pid_to_tid_.end()) {
      return;
    }

    char process_name[64] = {};
    const int name_rc = sceKernelGetProcessName(pid, process_name);
    app_info_t info {};
    const int app_info_rc = sceKernelGetAppInfo(pid, &info);
    if (app_info_rc != 0) {
      LOG_INFO("[JB][diag] %s pid=%d name_rc=%d name='%s' "
               "GetAppInfo rc=%d errno=%d",
               source, static_cast<int>(pid), name_rc, process_name,
               app_info_rc, errno);
      return;
    }
    char title_id[sizeof(info.title_id) + 1] = {};
    memcpy(title_id, info.title_id, sizeof(info.title_id));
    const std::string tid(title_id);
    const bool whitelisted =
        !tid.empty() &&
        is_whitelisted_app(tid, settings_.app_jailbreak_allowlist);
    LOG_INFO("[JB][diag] %s pid=%d name_rc=%d name='%s' appid=%u "
             "tid='%s' whitelist=%s",
             source, static_cast<int>(pid), name_rc, process_name, info.app_id,
             tid.c_str(),
             onion::app_jailbreak::whitelist_reason(
                 tid, settings_.app_jailbreak_allowlist));
    if (!whitelisted) {
      return;
    }

    struct kevent event;
    EV_SET(&event, static_cast<uintptr_t>(pid), EVFILT_PROC,
           EV_ADD | EV_ENABLE | EV_CLEAR, NOTE_EXIT, 0, nullptr);
    if (kevent(kq_, &event, 1, nullptr, 0, nullptr) != 0) {
      LOG_ERROR("[JB] cannot watch exit pid=%d tid=%s: %s",
                static_cast<int>(pid), tid.c_str(), strerror(errno));
      return;
    }

    TrackedApp &app = active_apps_[tid];
    const bool first_process = app.pids.empty();
    app.app_id = info.app_id;
    app.pids.insert(pid);
    pid_to_tid_[pid] = tid;

    LOG_INFO("[JB] allowed App %s pid=%d tid=%s appid=%u whitelist=%s",
             source, static_cast<int>(pid), tid.c_str(), info.app_id,
             onion::app_jailbreak::whitelist_reason(
                 tid, settings_.app_jailbreak_allowlist));

    if (first_process) {
      ensure_sandbox_root_watch();
      discover_slots(tid);
    }
  }

  void remove_app_process(pid_t pid) {
    const auto pid_it = pid_to_tid_.find(pid);
    if (pid_it == pid_to_tid_.end()) {
      return;
    }
    const std::string tid = pid_it->second;
    pid_to_tid_.erase(pid_it);

    const auto app_it = active_apps_.find(tid);
    if (app_it == active_apps_.end()) {
      return;
    }
    app_it->second.pids.erase(pid);
    if (!app_it->second.pids.empty()) {
      return;
    }

    active_apps_.erase(app_it);
    remove_tid_watches(tid);
    LOG_INFO("[JB] allowed App exited tid=%s; sandbox watches removed",
             tid.c_str());
    if (active_apps_.empty()) {
      close_vnode_watches();
    }
  }

  bool add_vnode_watch(WatchKind kind, const std::string &path,
                       const std::string &tid = {},
                       const std::string &slot_path = {}) {
    if (path_to_fd_.find(path) != path_to_fd_.end()) {
      return true;
    }

    int flags = O_RDONLY | O_NONBLOCK;
    const bool is_directory = kind != WatchKind::RequestFile;
    if (is_directory) {
      flags |= O_DIRECTORY;
    }
    const int fd = open(path.c_str(), flags);
    if (fd < 0) {
      LOG_ERROR("[JB][diag] open watcher failed kind=%s path=%s errno=%d (%s)",
                watch_kind_name(kind), path.c_str(), errno, strerror(errno));
      return false;
    }

    const uint32_t vnode_flags =
        is_directory
            ? NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE |
                  NOTE_RENAME | NOTE_REVOKE
            : NOTE_WRITE | NOTE_EXTEND | NOTE_CLOSE_WRITE | NOTE_DELETE |
                  NOTE_RENAME | NOTE_REVOKE;
    struct kevent event;
    EV_SET(&event, static_cast<uintptr_t>(fd), EVFILT_VNODE,
           EV_ADD | EV_ENABLE | EV_CLEAR, vnode_flags, 0, nullptr);
    if (kevent(kq_, &event, 1, nullptr, 0, nullptr) != 0) {
      LOG_ERROR("[JB] cannot watch path=%s: %s", path.c_str(),
                strerror(errno));
      close(fd);
      return false;
    }

    vnode_watches_.emplace(
        fd, VnodeWatch{fd, kind, path, tid, slot_path});
    path_to_fd_.emplace(path, fd);
    LOG_INFO("[JB][diag] vnode watch added kind=%s fd=%d path=%s tid=%s",
             watch_kind_name(kind), fd, path.c_str(),
             tid.empty() ? "-" : tid.c_str());
    return true;
  }

  void remove_vnode_watch(int fd) {
    const auto it = vnode_watches_.find(fd);
    if (it == vnode_watches_.end()) {
      return;
    }
    path_to_fd_.erase(it->second.path);
    close(it->second.fd);
    vnode_watches_.erase(it);
  }

  void remove_watch_tree(const std::string &root) {
    std::vector<int> remove;
    for (const auto &entry : vnode_watches_) {
      if (path_is_within(entry.second.path, root)) {
        remove.push_back(entry.first);
      }
    }
    for (const int fd : remove) {
      remove_vnode_watch(fd);
    }
  }

  void remove_tid_watches(const std::string &tid) {
    std::vector<int> remove;
    for (const auto &entry : vnode_watches_) {
      if (entry.second.tid == tid) {
        remove.push_back(entry.first);
      }
    }
    for (const int fd : remove) {
      remove_vnode_watch(fd);
    }
  }

  void close_vnode_watches() {
    for (const auto &entry : vnode_watches_) {
      close(entry.second.fd);
    }
    vnode_watches_.clear();
    path_to_fd_.clear();
  }

  void ensure_sandbox_root_watch() {
    if (active_apps_.empty()) {
      return;
    }
    if (path_is_directory(kSandboxRoot)) {
      const auto mount_it = path_to_fd_.find(kMountRoot);
      if (mount_it != path_to_fd_.end()) {
        remove_vnode_watch(mount_it->second);
      }
      (void)add_vnode_watch(WatchKind::SandboxRoot, kSandboxRoot);
      return;
    }

    LOG_INFO("[JB][diag] sandbox root absent; watching %s", kMountRoot);
    remove_watch_tree(kSandboxRoot);
    if (!add_vnode_watch(WatchKind::MountRoot, kMountRoot)) {
      LOG_ERROR("[JB] cannot watch %s while waiting for sandbox root: %s",
                kMountRoot, strerror(errno));
    }
  }

  void discover_all_slots() {
    ensure_sandbox_root_watch();
    if (path_to_fd_.find(kSandboxRoot) == path_to_fd_.end()) {
      return;
    }
    for (const auto &app : active_apps_) {
      discover_slots(app.first);
    }
  }

  void discover_slots(const std::string &tid) {
    if (active_apps_.find(tid) == active_apps_.end() ||
        !path_is_directory(kSandboxRoot)) {
      LOG_INFO("[JB][diag] slot discovery deferred tid=%s sandbox_root=%d",
               tid.c_str(), path_is_directory(kSandboxRoot) ? 1 : 0);
      return;
    }

    int found = 0;
    for (int slot = 0; slot <= 50; ++slot) {
      char suffix[5];
      snprintf(suffix, sizeof(suffix), "_%03d", slot);
      const std::string slot_path =
          std::string(kSandboxRoot) + "/" + tid + suffix;
      if (!path_is_directory(slot_path)) {
        continue;
      }
      ++found;
      LOG_INFO("[JB][diag] sandbox slot found tid=%s slot=%03d path=%s",
               tid.c_str(), slot, slot_path.c_str());
      (void)add_vnode_watch(WatchKind::SlotDirectory, slot_path, tid,
                            slot_path);
      ensure_download_watch(tid, slot_path);
    }
    LOG_INFO("[JB][diag] slot discovery complete tid=%s checked=51 found=%d",
             tid.c_str(), found);
  }

  void ensure_download_watch(const std::string &tid,
                             const std::string &slot_path) {
    if (active_apps_.find(tid) == active_apps_.end()) {
      return;
    }
    const std::string download_path = slot_path + "/download0";
    if (!path_is_directory(download_path)) {
      remove_watch_tree(download_path);
      return;
    }
    (void)add_vnode_watch(WatchKind::DownloadDirectory, download_path, tid,
                          slot_path);
    ensure_request_watches(tid, slot_path);
  }

  void ensure_request_watches(const std::string &tid,
                              const std::string &slot_path) {
    const std::string download_path = slot_path + "/download0";
    for (const char *name : kJailbreakReqNames) {
      const std::string request_path = download_path + "/" + name;
      if (path_to_fd_.find(request_path) != path_to_fd_.end()) {
        continue;
      }
      struct stat st {};
      if (stat(request_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        continue;
      }
      if (!add_vnode_watch(WatchKind::RequestFile, request_path, tid,
                           slot_path)) {
        continue;
      }
      const int request_fd = path_to_fd_[request_path];
      LOG_INFO("[JB] request file discovered: %s", request_path.c_str());
      if (process_request(request_fd, 0)) {
        remove_vnode_watch(request_fd);
      }
    }
  }

  void handle_vnode_event(const struct kevent &event) {
    const int fd = static_cast<int>(event.ident);
    const auto it = vnode_watches_.find(fd);
    if (it == vnode_watches_.end()) {
      return;
    }
    const VnodeWatch watch = it->second;
    const bool invalidated =
        (event.fflags & (NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE)) != 0;
    LOG_INFO("[JB][diag] vnode event kind=%s fd=%d fflags=0x%x path=%s",
             watch_kind_name(watch.kind), fd, event.fflags,
             watch.path.c_str());

    switch (watch.kind) {
    case WatchKind::MountRoot:
      ensure_sandbox_root_watch();
      if (path_is_directory(kSandboxRoot)) {
        discover_all_slots();
      }
      break;
    case WatchKind::SandboxRoot:
      if (invalidated) {
        remove_watch_tree(kSandboxRoot);
        ensure_sandbox_root_watch();
      } else {
        discover_all_slots();
      }
      break;
    case WatchKind::SlotDirectory:
      if (invalidated) {
        remove_watch_tree(watch.path);
        discover_slots(watch.tid);
      } else {
        ensure_download_watch(watch.tid, watch.slot_path);
      }
      break;
    case WatchKind::DownloadDirectory:
      if (invalidated) {
        remove_watch_tree(watch.path);
        ensure_download_watch(watch.tid, watch.slot_path);
      } else {
        ensure_request_watches(watch.tid, watch.slot_path);
      }
      break;
    case WatchKind::RequestFile:
      if (invalidated) {
        remove_vnode_watch(fd);
        ensure_request_watches(watch.tid, watch.slot_path);
      } else if (event.fflags &
                 (NOTE_WRITE | NOTE_EXTEND | NOTE_CLOSE_WRITE)) {
        if (process_request(fd, event.fflags)) {
          remove_vnode_watch(fd);
        }
      }
      break;
    }
  }

  bool read_request(int fd, std::string *body, bool *too_large) const {
    *too_large = false;
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
      return false;
    }
    if (static_cast<uint64_t>(st.st_size) > kMaxRequestBytes) {
      *too_large = true;
      return false;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
      return false;
    }

    body->assign(static_cast<size_t>(st.st_size), '\0');
    size_t offset = 0;
    while (offset < body->size()) {
      const ssize_t count =
          read(fd, body->data() + offset, body->size() - offset);
      if (count < 0 && errno == EINTR) {
        continue;
      }
      if (count <= 0) {
        return false;
      }
      offset += static_cast<size_t>(count);
    }
    return true;
  }

  pid_t parse_request_pid(const cJSON *root) const {
    const cJSON *value = onion_cjson::item(root, "PID");
    if (cJSON_IsNumber(value)) {
      if (value->valuedouble != static_cast<double>(value->valueint) ||
          value->valueint <= 1) {
        return -1;
      }
      return static_cast<pid_t>(value->valueint);
    }
    if (!cJSON_IsString(value) || value->valuestring == nullptr) {
      return -1;
    }

    errno = 0;
    char *end = nullptr;
    const long parsed = strtol(value->valuestring, &end, 10);
    if (errno != 0 || end == value->valuestring || *end != '\0' ||
        parsed <= 1 || parsed > INT_MAX) {
      return -1;
    }
    return static_cast<pid_t>(parsed);
  }

  bool clear_request(const std::string &path) const {
    if (unlink(path.c_str()) != 0 && errno != ENOENT) {
      LOG_ERROR("[JB] unlink request failed path=%s: %s", path.c_str(),
                strerror(errno));
      return false;
    } else {
      LOG_INFO("[JB] cleared request file %s", path.c_str());
    }
    return true;
  }

  bool process_request(int fd, uint32_t event_flags) {
    const auto watch_it = vnode_watches_.find(fd);
    if (watch_it == vnode_watches_.end()) {
      return false;
    }
    const std::string path = watch_it->second.path;
    const std::string tid = watch_it->second.tid;

    std::string body;
    bool too_large = false;
    if (!read_request(fd, &body, &too_large)) {
      if (too_large) {
        LOG_ERROR("[JB] request exceeds %lu bytes: %s",
                  static_cast<unsigned long>(kMaxRequestBytes), path.c_str());
        return clear_request(path);
      }
      return false;
    }

    onion_cjson::Root json(body);
    if (!json) {
      if (event_flags & NOTE_CLOSE_WRITE) {
        LOG_ERROR("[JB] incomplete/invalid request JSON retained for retry: %s",
                  path.c_str());
      }
      return false;
    }

    const pid_t target_pid = parse_request_pid(json.get());
    if (target_pid <= 1) {
      LOG_ERROR("[JB] invalid or missing PID in request: %s", path.c_str());
      return clear_request(path);
    }

    const onion::Settings current = g_settings.snapshot();
    if (!jb_listener_enabled.load(std::memory_order_acquire) ||
        !current.app_jailbreak_enabled ||
        active_apps_.find(tid) == active_apps_.end() ||
        !is_whitelisted_app(tid, current.app_jailbreak_allowlist)) {
      LOG_INFO("[JB] request deferred because App jailbreak is inactive: %s",
               path.c_str());
      return false;
    }

    if (!isProcessAlive(target_pid)) {
      LOG_ERROR("[JB] pid=%d is dead; clearing stale request %s",
                static_cast<int>(target_pid), path.c_str());
      return clear_request(path);
    }
    if (KERNEL_ADDRESS_ALLPROC == 0) {
      LOG_ERROR("[JB] KERNEL_ADDRESS_ALLPROC=0; clearing request %s",
                path.c_str());
      return clear_request(path);
    }

    const int uid_before = jb_read_uid(target_pid);
    const uint64_t auth_before = jb_read_authid(target_pid);
    LOG_INFO("[JB] request pid=%d tid=%s pre-jb uid=%d authid=0x%llx",
             static_cast<int>(target_pid), tid.c_str(), uid_before,
             static_cast<unsigned long long>(auth_before));

    bool ok = false;
    for (int attempt = 1; attempt <= kProcResolveAttempts && !ok; ++attempt) {
      if (kernel_get_proc(target_pid) == 0) {
        LOG_INFO("[JB] kernel_get_proc(%d)=0 attempt=%d/%d",
                 static_cast<int>(target_pid), attempt, kProcResolveAttempts);
        if (!isProcessAlive(target_pid)) {
          break;
        }
        if (attempt < kProcResolveAttempts) {
          usleep(kProcResolveUsleep);
        }
        continue;
      }
      if (!jb_listener_enabled.load(std::memory_order_acquire)) {
        LOG_INFO("[JB] listener disabled before privilege apply; deferring %s",
                 path.c_str());
        return false;
      }
      ok = jb_apply_privileges(target_pid, /*escape_sandbox=*/true);
      break;
    }

    const int uid_after = jb_read_uid(target_pid);
    const uint64_t auth_after = jb_read_authid(target_pid);
    LOG_INFO("[JB] post-jb pid=%d uid=%d (was %d) authid=0x%llx "
             "(was 0x%llx) ok=%d",
             static_cast<int>(target_pid), uid_after, uid_before,
             static_cast<unsigned long long>(auth_after),
             static_cast<unsigned long long>(auth_before), ok ? 1 : 0);

    if (ok && uid_after == 0) {
      if (current.debug_app_jb_msg) {
        onion_notify(true, "notify.jailbreak.granted",
                     static_cast<int>(target_pid));
      }
      LOG_INFO("[JB] OK: pid=%d tid=%s fully jailbroken",
               static_cast<int>(target_pid), tid.c_str());
    } else {
      LOG_ERROR("[JB] FAIL: privilege apply did not stick for pid=%d uid=%d",
                static_cast<int>(target_pid), uid_after);
    }

    return clear_request(path);
  }

  int control_read_fd_ = -1;
  int kq_ = -1;
  pid_t syscore_pid_ = -1;
  onion::Settings settings_ {};
  std::map<int, VnodeWatch> vnode_watches_;
  std::map<std::string, int> path_to_fd_;
  std::map<pid_t, std::string> pid_to_tid_;
  std::map<std::string, TrackedApp> active_apps_;
};

bool set_nonblocking_close_on_exec(int fd) {
  const int status_flags = fcntl(fd, F_GETFL, 0);
  const int descriptor_flags = fcntl(fd, F_GETFD, 0);
  return status_flags >= 0 && descriptor_flags >= 0 &&
         fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) == 0 &&
         fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) == 0;
}

} // namespace

void app_jailbreak_set_enabled(bool enabled) {
  const bool previous =
      jb_listener_enabled.exchange(enabled, std::memory_order_acq_rel);

  pthread_mutex_lock(&jb_control_lock);
  if (jb_control_write_fd >= 0) {
    const char wake = 1;
    const ssize_t ignored = write(jb_control_write_fd, &wake, sizeof(wake));
    (void)ignored;
  }
  pthread_mutex_unlock(&jb_control_lock);

  if (previous != enabled) {
    LOG_INFO("[JB] app jailbreak listener %s",
             enabled ? "enabled" : "disabled");
  }
}

void *fifo_and_dumper_thread(void *args) noexcept {
  (void)args;
  int control_pipe[2] = {-1, -1};
  if (pipe(control_pipe) != 0 ||
      !set_nonblocking_close_on_exec(control_pipe[0]) ||
      !set_nonblocking_close_on_exec(control_pipe[1])) {
    LOG_ERROR("[JB] control pipe setup failed: %s", strerror(errno));
    if (control_pipe[0] >= 0) {
      close(control_pipe[0]);
    }
    if (control_pipe[1] >= 0) {
      close(control_pipe[1]);
    }
    return nullptr;
  }

  pthread_mutex_lock(&jb_control_lock);
  jb_control_write_fd = control_pipe[1];
  pthread_mutex_unlock(&jb_control_lock);

  LOG_INFO("[JB] worker started (SceSysCore lifecycle + sandbox vnode events)");
  LOG_INFO("[JB] kernel symbols: ALLPROC=0x%lx ROOTVNODE=0x%lx",
           static_cast<unsigned long>(KERNEL_ADDRESS_ALLPROC),
           static_cast<unsigned long>(KERNEL_ADDRESS_ROOTVNODE));

  {
    JailbreakEventLoop loop(control_pipe[0]);
    loop.run();
  }

  pthread_mutex_lock(&jb_control_lock);
  if (jb_control_write_fd == control_pipe[1]) {
    jb_control_write_fd = -1;
  }
  close(control_pipe[1]);
  pthread_mutex_unlock(&jb_control_lock);
  close(control_pipe[0]);

  LOG_INFO("[JB] event listener worker stopped");
  return nullptr;
}
