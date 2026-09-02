/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Launch the owned util, kstuff and daemon dependency chain. */
int bootstrap_launch_services(uint32_t firmware_version, bool kstuff_autoload);

#ifdef __cplusplus
}
#endif
