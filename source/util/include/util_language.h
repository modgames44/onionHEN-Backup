/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * System-language snapshot owned by the util process.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Refresh the cached PS5 system language.
 *
 * Call this before util switches to PTRACE_AUTHID. A failed query preserves
 * the last valid value instead of silently replacing it with English.
 */
bool util_refresh_system_language(void);

/** Return the last successfully queried PS5 system language (English first). */
int util_cached_system_language(void);

/** Apply an explicit or system-backed Toolbox language to util notifications. */
void util_apply_ui_language(int ui_language);

#ifdef __cplusplus
}
#endif
