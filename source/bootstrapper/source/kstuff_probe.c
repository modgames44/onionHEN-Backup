/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "kstuff_probe.h"

#include <onion/proc_query.h>
#include <stddef.h>
#include <string.h>

int sceKernelMprotect(void *addr, size_t len, int prot);

bool kstuff_already_running(void) {
  if (onion_find_pid("kstuff.elf") > 0 || onion_find_pid("kstuff") > 0 ||
      onion_find_pid_substr("kstuff") > 0)
    return true;

  char probe[100];
  memset(probe, 0, sizeof(probe));
  return sceKernelMprotect(probe, sizeof(probe), 0x7) == 0;
}
