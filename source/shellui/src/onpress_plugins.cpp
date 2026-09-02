/* Copyright (C) 2025 OnionHEN / LightningMods - OnPress built-in plugins */

#include "onpress.hpp"
#include "shellui_payload_state.hpp"

#include <cerrno>
#include <climits>
#include <cstdlib>

namespace {

OnPressResult toggle_plugin_now(OnPressContext &ctx, DaemonCommands cmd,
                                const char *fail_notify,
                                const char *on_notify, const char *off_notify) {
  ctx.dirty = false;
  const bool enabled = value_as_int(ctx);
  /* Stop is idempotent and must clear util's desired state even when a
   * listener failed before the UI could observe it as running. */
  if (enabled && IPC_Client::getInstance(true).FtpStatus())
    return OnPressResult::EarlyReturn;

  if (IPC_Client::getInstance(true).ToggleSetting(cmd, enabled) !=
      IPC_Ret::NO_ERROR) {
    notify(fail_notify);
    return OnPressResult::EarlyReturn;
  }
  notify(enabled ? on_notify : off_notify);
  return OnPressResult::Handled;
}

OnPressResult toggle_next_boot(OnPressContext &ctx, bool &field,
                               const char *on_notify, const char *off_notify) {
  const bool enabled = value_as_int(ctx);
  if (enabled == field)
    return OnPressResult::EarlyReturn;
  field = enabled;
  notify(enabled ? on_notify : off_notify);
  return OnPressResult::Handled;
}

} // namespace

OnPressResult onpress_ftp_run(OnPressContext &ctx) {
  return toggle_plugin_now(ctx, BREW_UTIL_TOGGLE_FTP,
                           "notify.ftp.toggle_failed", "notify.ftp.enabled",
                           "notify.ftp.disabled");
}

OnPressResult onpress_ftp_autoload(OnPressContext &ctx) {
  return toggle_next_boot(ctx, g_settings.ftp_autoload,
                          "notify.ftp.next_boot_on", "notify.ftp.next_boot_off");
}

OnPressResult onpress_ftp_port(OnPressContext &ctx) {
  char *end = nullptr;
  errno = 0;
  const long parsed = std::strtol(ctx.value.c_str(), &end, 10);
  if (errno != 0 || end == ctx.value.c_str() || *end != '\0' || parsed < 1 ||
      parsed > 65535) {
    notify("notify.ftp.port_invalid");
    return OnPressResult::EarlyReturn;
  }

  const int port = static_cast<int>(parsed);
  if (port == g_settings.ftp_port)
    return OnPressResult::EarlyReturn;
  g_settings.ftp_port = port;
  /* settings_commit persists the value and asks util to reconfigure a live
   * listener.  The current run state is intentionally left unchanged. */
  ctx.reload_util = true;
  notify("notify.ftp.port_changed", port);
  return OnPressResult::Handled;
}

/*
 * The Plugins page lists each built-in plugin as a <link> that the stock
 * settings UI navigates natively (file="<plugin>.xml"). Each plugin's config
 * page then binds its controls to the shared handlers below, so start/stop and
 * scan behavior stay in one place.
 */
static const OnPressExactEntry kPluginsExact[] = {
    {"id_plugin_kstuff_autoload", onpress_kstuff_autoload},
    {"id_plugin_delete_kstuff", onpress_delete_kstuff},
    {"id_plugin_ftpsrv_run", onpress_ftp_run},
    {"id_plugin_ftpsrv_autoload", onpress_ftp_autoload},
    {"id_plugin_ftpsrv_port", onpress_ftp_port},
};

const OnPressExactEntry *onpress_plugins_exact(size_t *count) {
  *count = sizeof(kPluginsExact) / sizeof(kPluginsExact[0]);
  return kPluginsExact;
}
