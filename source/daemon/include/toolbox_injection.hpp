/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Serializes Toolbox injection and binds readiness to one SceShellUI process
 * instance.  Policy (delays, notifications, payload selection) stays with the
 * daemon command; this class owns only injection lifecycle coordination.
 */
#pragma once

#include <mutex>
#include <sys/types.h>

#include <onion/ready.h>

namespace onion {

enum class ToolboxInjectionResult {
  AlreadyReady,
  Injected,
  TargetNotFound,
  InjectFailed,
  ReadyTimeout,
  TargetChanged,
};

struct ToolboxInjectionOutcome {
  ToolboxInjectionResult result;
  pid_t pid;

  bool ready() const noexcept {
    return result == ToolboxInjectionResult::AlreadyReady ||
           result == ToolboxInjectionResult::Injected;
  }
};

class ToolboxInjectionCoordinator final {
public:
  bool is_ready_for(pid_t pid) const noexcept {
    return onion_ready_matches_pid(ONION_READY_TOOLBOX, pid);
  }

  template <typename ResolvePid, typename Inject>
  ToolboxInjectionOutcome ensure(ResolvePid resolve_pid, Inject inject,
                                  int timeout_ms, int poll_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    const pid_t pid = resolve_pid();
    if (pid <= 0) {
      return {ToolboxInjectionResult::TargetNotFound, pid};
    }
    if (is_ready_for(pid)) {
      return {ToolboxInjectionResult::AlreadyReady, pid};
    }

    // A marker for another PID belongs to an old ShellUI instance.
    onion_ready_clear(ONION_READY_TOOLBOX);
    if (!inject(pid)) {
      onion_ready_clear(ONION_READY_TOOLBOX);
      return {ToolboxInjectionResult::InjectFailed, pid};
    }
    if (!onion_ready_wait_pid(ONION_READY_TOOLBOX, pid, timeout_ms, poll_ms)) {
      onion_ready_clear(ONION_READY_TOOLBOX);
      return {ToolboxInjectionResult::ReadyTimeout, pid};
    }

    // Do not let an acknowledgement from a dying instance mark its
    // replacement as initialized.
    if (resolve_pid() != pid) {
      onion_ready_clear(ONION_READY_TOOLBOX);
      return {ToolboxInjectionResult::TargetChanged, pid};
    }
    return {ToolboxInjectionResult::Injected, pid};
  }

private:
  mutable std::mutex mutex_;
};

} // namespace onion
