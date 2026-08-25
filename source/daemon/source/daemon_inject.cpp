/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "daemon_ops.hpp"
#include "globalconf.hpp"
#include "toolbox_injection.hpp"
#include <onion/platform.h>
#include <onion/proc_query.h>
#include <onion/ready.h>
#include <onion/settings.hpp>
#include <onion/toolbox_timing.h>
#include "globalconf.hpp"
#include <unistd.h>

extern "C" {
int sceKernelMprotect(void *addr, size_t len, int prot);
bool Inject_Toolbox(int pid, uint8_t *elf);
extern uint8_t shellui_elf_start[];
extern const unsigned int shellui_elf_size;
}

namespace {

onion::ToolboxInjectionCoordinator g_toolbox_injection;

} // namespace

bool cmd_enable_toolbox(){
    char buz[100] = {0};

    /*
     * Fast path for daemon restart / repeated enable requests.  ensure() does
     * the same check again while holding its mutex, so this unlocked probe is
     * only an optimization and cannot permit a duplicate injection.
     */
    const pid_t observed_pid = static_cast<pid_t>(get_shellui_pid());
    if (observed_pid > 0 && g_toolbox_injection.is_ready_for(observed_pid)) {
      LOG_WARN("Toolbox already active in SceShellUI pid=%d",
                   static_cast<int>(observed_pid));
      return true;
    }

    /*
     * If kstuff is present, wait until mprotect works (patches applied) before
     * we ptrace ShellUI. Injecting while kstuff is still patching ShellUI
     * causes "waiting for toolbox" forever / ShellUI crash.
     * (Race seen when daemon+kstuff launched close together via elfldr.)
     * Note: we do NOT pause/resume kstuff — other tools may own that; only wait for
     * mprotect readiness.
     */
    if (find_pid("kstuff.elf") > 0 || find_pid("kstuff") > 0) {
      LOG_INFO("kstuff present — waiting for mprotect before toolbox inject");
      for (int i = 0; i < 20; i++) {
        if (sceKernelMprotect(&buz[0], 100, 0x7) == 0)
          break;
        sleep(1);
      }
      sleep(2);
    }

    LOG_INFO("Activating toolbox...");
    /*
     * rest_mode.resume_reinject_delay_seconds only on rest resume — never on
     * cold start.
     * util_booted is true almost immediately after util starts (before this
     * inject), so gating on it alone hung first toolbox load for delay seconds.
     * Rest re-activation delay lives in util patch_checker / check_addr_change.
     */
    LoadSettings();
    {
      const uint64_t delay = g_settings.snapshot().rest_mode_delay_seconds;
      constexpr bool kRestResume = false; /* daemon cold/direct inject path */
      if (onion_toolbox_should_apply_rest_delay(kRestResume, delay)) {
        LOG_INFO("rest delay %llu (rest resume path)",
                     static_cast<unsigned long long>(delay));
        sleep(static_cast<unsigned int>(delay));
      }
    }

    /* Prefer kstuff ready marker when present; fall back to short settle. */
    if (!onion_ready_wait(ONION_READY_KSTUFF, /*timeout_ms=*/5000, /*poll_ms=*/200)) {
      sleep(1);
    }

    const onion::ToolboxInjectionOutcome outcome = g_toolbox_injection.ensure(
        []() -> pid_t { return static_cast<pid_t>(get_shellui_pid()); },
        [](pid_t pid) -> bool {
          LOG_INFO("Injecting toolbox into SceShellUI pid=%d",
                       static_cast<int>(pid));
          return Inject_Toolbox(static_cast<int>(pid), shellui_elf_start);
        },
        /*timeout_ms=*/45 * 1000, /*poll_ms=*/250);

    switch (outcome.result) {
    case onion::ToolboxInjectionResult::AlreadyReady:
      LOG_WARN("Toolbox already active in SceShellUI pid=%d",
                   static_cast<int>(outcome.pid));
      return true;
    case onion::ToolboxInjectionResult::Injected:
      LOG_INFO("Toolbox online for SceShellUI pid=%d (ready protocol)",
                   static_cast<int>(outcome.pid));
      return true;
    case onion::ToolboxInjectionResult::TargetNotFound:
      onion_notify(true, "notify.toolbox.no_shellui_pid");
      return false;
    case onion::ToolboxInjectionResult::InjectFailed:
      /* Do NOT ForceKill ShellUI — that loops home menu / coredumps. */
      onion_notify(true, "notify.toolbox.inject_failed");
      return false;
    case onion::ToolboxInjectionResult::ReadyTimeout:
      onion_notify(true,
                   "notify.toolbox.load_timeout");
      return false;
    case onion::ToolboxInjectionResult::TargetChanged:
      onion_notify(true,
                   "notify.toolbox.shellui_restarted");
      return false;
    }

    return false;
}
