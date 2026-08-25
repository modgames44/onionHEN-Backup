/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Detect whether kstuff is already present (process and/or patches).
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * True if a kstuff process is running, or if userland mprotect already
 * succeeds (kernel patches active). Used to skip a second elfldr load.
 */
bool kstuff_already_running(void);

#ifdef __cplusplus
}
#endif
