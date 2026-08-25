/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include <onion/platform.h>
/* C++ headers before common_utils.h: the latter pulls stdatomic.h, which
 * breaks libc++ <atomic> if included first. */
#include <onion/settings.hpp>
#include "common_utils.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

// External C declarations
extern "C" {
#include "common_utils.h"

// Atomic state variables
atomic_bool rest_mode_action = false;
atomic_bool no_network_rest_mode_action = false;
atomic_bool no_network_patched = false;
atomic_bool real_rest_mode_detected = false;
}

// Global variables
char ip_address[40];
static pthread_mutex_t ip_lock = PTHREAD_MUTEX_INITIALIZER;
extern atomic_bool not_connected;

// Network monitoring
void check_addr_change(void) {
  pthread_mutex_lock(&ip_lock);
  char func_ip_address[40];

  if (get_ip_address(&func_ip_address[0], sizeof(func_ip_address)) < 0) {
    pthread_mutex_unlock(&ip_lock);
    return;
  }

  if (get_ip_address(&ip_address[0], sizeof(ip_address)) < 0) {
    pthread_mutex_unlock(&ip_lock);
    return;
  }

  bool ip_changed = strcmp(&ip_address[0], &func_ip_address[0]) != 0;
  if (ip_changed) {
    onion_notify(true, "notify.net.ip_changed", func_ip_address);
    /* Clear rest-mode side flags; toolbox reinject is handled on the
     * network-down path / ShellUI standby, not via TCP CMD. */
    (void)rest_mode_action;
    real_rest_mode_detected = not_connected = no_network_patched =
        rest_mode_action = false;
  }

  pthread_mutex_unlock(&ip_lock);
}

void *ip_thread(void *arg) {
  (void)arg;
  do {
    sleep(1);
  } while (get_ip_address(&ip_address[0], sizeof(ip_address)) < 0);

  while (true) {
    check_addr_change();
    sleep(2);
  }
}

void start_ip_thread(void) {
  pthread_t ip_thread_thr;
  pthread_create(&ip_thread_thr, NULL, ip_thread, NULL);
  pthread_detach(ip_thread_thr);
}
