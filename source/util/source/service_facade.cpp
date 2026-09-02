/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * The facade is the only util-facing boundary for the in-process FTP module.
 * It owns the serving thread and keeps UI/IPC code independent from the
 * third-party implementation details.
 */

#include "service_facade.hpp"

#include <onion/builtin_services.h>
#include <onion/platform.h>

#include "srv.h"

#include <stddef.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

namespace {

struct FtpRuntime {
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_t operation_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_t thread = {};
  bool running = false;
  bool thread_created = false;
  bool desired_enabled = false;
  bool listener_ready = false;
  uint16_t port = ONION_FTPSRV_PORT;
};

FtpRuntime g_runtime;

constexpr int kListenerReadyWaitMs = 2000;
constexpr useconds_t kRecoveryRetryDelayUs[] = {
    250 * 1000,
    500 * 1000,
    1000 * 1000,
    2000 * 1000,
};

void *ftp_thread_main(void *arg) {
  const uint16_t port = *static_cast<const uint16_t *>(arg);
  const int result = ftp_serve(port, /*notify_user=*/1);

  pthread_mutex_lock(&g_runtime.mutex);
  g_runtime.running = false;
  g_runtime.listener_ready = false;
  pthread_mutex_unlock(&g_runtime.mutex);

  if (result == FTP_SERVE_BIND_FAILED) {
    LOG_ERROR("ftpsrv failed to bind TCP %u", static_cast<unsigned>(port));
  } else if (result < 0) {
    LOG_ERROR("ftpsrv stopped with error %d", result);
  } else {
    LOG_INFO("ftpsrv stopped on TCP %u", static_cast<unsigned>(port));
  }
  return nullptr;
}

bool valid_port(uint16_t port) { return port != 0; }

void stop_thread() {
  pthread_t thread = {};
  bool join = false;

  pthread_mutex_lock(&g_runtime.mutex);
  if (g_runtime.thread_created) {
    ftp_server_stop();
    thread = g_runtime.thread;
    join = true;
  }
  pthread_mutex_unlock(&g_runtime.mutex);

  if (!join) {
    return;
  }

  pthread_join(thread, nullptr);
  pthread_mutex_lock(&g_runtime.mutex);
  g_runtime.running = false;
  g_runtime.listener_ready = false;
  g_runtime.thread_created = false;
  pthread_mutex_unlock(&g_runtime.mutex);
}

bool wait_for_listener_ready() {
  for (int waited = 0; waited < kListenerReadyWaitMs; waited += 50) {
    if (ftp_server_is_listening()) {
      pthread_mutex_lock(&g_runtime.mutex);
      g_runtime.listener_ready = true;
      pthread_mutex_unlock(&g_runtime.mutex);
      return true;
    }

    pthread_mutex_lock(&g_runtime.mutex);
    const bool active = g_runtime.running;
    pthread_mutex_unlock(&g_runtime.mutex);
    if (!active) {
      return false;
    }
    usleep(50 * 1000);
  }
  const bool ready = ftp_server_is_listening() != 0;
  if (ready) {
    pthread_mutex_lock(&g_runtime.mutex);
    g_runtime.listener_ready = true;
    pthread_mutex_unlock(&g_runtime.mutex);
  }
  return ready;
}

bool start_thread(uint16_t port) {
  pthread_mutex_lock(&g_runtime.mutex);
  g_runtime.port = port;
  g_runtime.running = true;
  g_runtime.listener_ready = false;
  ftp_server_prepare();
  const int rc = pthread_create(&g_runtime.thread, nullptr, ftp_thread_main,
                                &g_runtime.port);
  if (rc == 0) {
    g_runtime.thread_created = true;
  } else {
    g_runtime.running = false;
  }
  pthread_mutex_unlock(&g_runtime.mutex);

  if (rc != 0) {
    LOG_ERROR("Failed to create ftpsrv thread: %d", rc);
    return false;
  }
  return wait_for_listener_ready();
}

} // namespace

namespace onion::services {

bool FtpServiceFacade::start(uint16_t port) {
  if (!valid_port(port)) {
    LOG_ERROR("Refusing invalid FTP port %u", static_cast<unsigned>(port));
    return false;
  }

  pthread_mutex_lock(&g_runtime.operation_mutex);
  g_runtime.desired_enabled = true;
  stop_thread();

  const bool ok = start_thread(port);
  if (!ok) {
    stop_thread();
  }
  pthread_mutex_unlock(&g_runtime.operation_mutex);

  if (!ok) {
    LOG_ERROR("Failed to start ftpsrv on TCP %u",
              static_cast<unsigned>(port));
    return false;
  }

  LOG_INFO("ftpsrv started on TCP %u", static_cast<unsigned>(port));
  return true;
}

void FtpServiceFacade::stop() {
  pthread_mutex_lock(&g_runtime.operation_mutex);
  g_runtime.desired_enabled = false;
  stop_thread();
  pthread_mutex_unlock(&g_runtime.operation_mutex);
}

bool FtpServiceFacade::reconfigure(uint16_t port) {
  if (!valid_port(port)) {
    return false;
  }

  pthread_mutex_lock(&g_runtime.operation_mutex);
  pthread_mutex_lock(&g_runtime.mutex);
  const bool desired = g_runtime.desired_enabled;
  const uint16_t previous_port = g_runtime.port;
  pthread_mutex_unlock(&g_runtime.mutex);

  if (!desired) {
    pthread_mutex_lock(&g_runtime.mutex);
    g_runtime.port = port;
    pthread_mutex_unlock(&g_runtime.mutex);
    pthread_mutex_unlock(&g_runtime.operation_mutex);
    return true;
  }

  stop_thread();
  bool ok = start_thread(port);
  if (!ok) {
    stop_thread();
    if (previous_port != port) {
      ok = start_thread(previous_port);
      if (!ok) {
        stop_thread();
      }
    }
  }
  pthread_mutex_unlock(&g_runtime.operation_mutex);

  if (!ok) {
    LOG_ERROR("ftpsrv failed to restore TCP %u after reconfigure failure",
              static_cast<unsigned>(previous_port));
  }
  return ok;
}

bool FtpServiceFacade::recover() {
  pthread_mutex_lock(&g_runtime.operation_mutex);
  pthread_mutex_lock(&g_runtime.mutex);
  const bool desired = g_runtime.desired_enabled;
  const uint16_t port = g_runtime.port;
  pthread_mutex_unlock(&g_runtime.mutex);

  if (!desired) {
    pthread_mutex_unlock(&g_runtime.operation_mutex);
    LOG_DEBUG("ftpsrv recovery skipped; service is disabled");
    return true;
  }

  stop_thread();
  constexpr size_t kRetryCount =
      sizeof(kRecoveryRetryDelayUs) / sizeof(kRecoveryRetryDelayUs[0]);
  for (size_t attempt = 0; attempt <= kRetryCount; ++attempt) {
    if (start_thread(port)) {
      pthread_mutex_unlock(&g_runtime.operation_mutex);
      LOG_INFO("ftpsrv recovered on TCP %u (attempt %zu)",
               static_cast<unsigned>(port), attempt + 1);
      return true;
    }

    stop_thread();
    if (attempt < kRetryCount) {
      usleep(kRecoveryRetryDelayUs[attempt]);
    }
  }

  pthread_mutex_unlock(&g_runtime.operation_mutex);
  LOG_ERROR("ftpsrv recovery exhausted on TCP %u",
            static_cast<unsigned>(port));
  return false;
}

bool FtpServiceFacade::running() const {
  pthread_mutex_lock(&g_runtime.mutex);
  const bool value = g_runtime.listener_ready;
  pthread_mutex_unlock(&g_runtime.mutex);
  return value;
}

uint16_t FtpServiceFacade::port() const {
  pthread_mutex_lock(&g_runtime.mutex);
  const uint16_t value = g_runtime.port;
  pthread_mutex_unlock(&g_runtime.mutex);
  return value;
}

FtpServiceFacade &ftpService() {
  static FtpServiceFacade service;
  return service;
}

} // namespace onion::services
