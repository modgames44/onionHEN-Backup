/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Payload ELF load helpers (PID files + elfldr socket).
 * OnionHEN only supports bare .elf payloads (no .plugin packages).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

bool onion_payload_is_elf(const void *buf, size_t size);

/**
 * Derive launch/PID key from basename: "foo.elf" → "foo".
 * Rejects empty stem (".elf") and path separators.
 */
bool onion_payload_elf_key_from_name(const char *name, char *out, size_t out_sz);

void onion_payload_pid_path(char *out, size_t out_sz, const char *title_id);
pid_t onion_payload_read_pid_file(const char *pid_path);
/** Persist only a real payload PID (>1); otherwise remove the marker. */
void onion_payload_write_pid_file(const char *pid_path, pid_t pid);

/** Kill process recorded in /system_tmp/onionhen/pid/<key>.PID if still valid. */
void onion_payload_stop_by_title(const char *title_id);

/**
 * Stage ELF under /data/OnionHEN/payloads/<key>.elf and launch exclusively via
 * OnionHEN's private 9020 elfldr. User payloads never fall back to the external
 * 9021 bootstrap/recovery loader and never infer a PID from process snapshots.
 *
 * Returns the loader-reported PID (>1) on success or -1 on every failure,
 * including an unavailable loader, protocol timeout, or missing/invalid PID.
 */
pid_t onion_payload_launch_elfldr(const char *title_id, const uint8_t *elf,
                                  size_t elf_sz);

/** malloc'd file contents; caller free(). NULL on error. */
uint8_t *onion_payload_read_file(const char *path, size_t *out_size);

/**
 * Load a payload .elf from @path.
 * @filename Optional basename. If NULL, derived from @path.
 * Returns true only when the private loader reports a real PID (>1) and the
 * PID file has been written.
 */
bool onion_payload_load(const char *path, const char *filename);

#ifdef __cplusplus
}
#endif
