/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Pure GitHub API helpers (no network).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Extract commit "sha" from a GitHub commits API JSON body.
 * Accepts either a single commit object or an array (uses first element).
 * Returns true and NUL-terminates @p sha_buffer on success.
 */
bool onion_http_extract_commit_sha(const char *json_data, char *sha_buffer,
                                   size_t buffer_size);

#ifdef __cplusplus
}
#endif
