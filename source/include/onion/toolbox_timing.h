/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Pure policy for toolbox inject rest-mode delay (host-testable).
 *
 * Cold start: util signals util_booted before daemon injects — that must NOT
 * apply rest_mode.resume_reinject_delay_seconds (would hang first toolbox load).
 * Rest resume: only then apply configured delay (util patch_checker path).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @param rest_resume  true only when recovering from rest mode (not cold boot)
 * @param delay_sec    configured rest_mode.resume_reinject_delay_seconds
 */
static inline bool onion_toolbox_should_apply_rest_delay(bool rest_resume,
                                                         uint64_t delay_sec) {
  return rest_resume && delay_sec > 0;
}

#ifdef __cplusplus
}
#endif
