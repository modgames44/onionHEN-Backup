/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Shared filesystem helpers (if_exists / touch / rmtree).
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True if path exists (stat succeeds). */
bool if_exists(const char *path);

/** Create or truncate empty file at path (mode 0777). */
bool touch_file(const char *path);

/** Recursively delete directory tree at path. */
bool rmtree(const char *path);

#ifdef __cplusplus
}
#endif
