/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Bridges the config schema to the logger.
 *
 * libonion_settings deliberately does not depend on libonion_platform, so the
 * level constants exist in both. This header is the one place that sees both
 * and is where the two are checked against each other.
 */

#pragma once

#include <onion/log.h>
#include <onion/settings.hpp>

namespace onion {

static_assert(kLogLevelOff == static_cast<int>(ONION_LOG_OFF),
              "config log level drifted from onion_log_level");
static_assert(kLogLevelError == static_cast<int>(ONION_LOG_ERROR),
              "config log level drifted from onion_log_level");
static_assert(kLogLevelWarn == static_cast<int>(ONION_LOG_WARN),
              "config log level drifted from onion_log_level");
static_assert(kLogLevelInfo == static_cast<int>(ONION_LOG_INFO),
              "config log level drifted from onion_log_level");
static_assert(kLogLevelDebug == static_cast<int>(ONION_LOG_DEBUG),
              "config log level drifted from onion_log_level");
static_assert(kLogLevelTrace == static_cast<int>(ONION_LOG_TRACE),
              "config log level drifted from onion_log_level");

/**
 * Apply the configured threshold to the logger.
 *
 * Levels stripped by ONION_LOG_COMPILE_LEVEL cannot be restored here — asking
 * for trace in a release build still yields info — so the effective level is
 * reported back for the caller to log.
 */
inline onion_log_level apply_log_settings(const Settings &s) {
  onion_log_level want = static_cast<onion_log_level>(s.log_level);
  if (want > ONION_LOG_COMPILE_LEVEL) {
    want = static_cast<onion_log_level>(ONION_LOG_COMPILE_LEVEL);
  }
  onion_log_set_level(want);
  return want;
}

} // namespace onion
