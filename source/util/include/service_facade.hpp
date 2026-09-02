/* Copyright (C) 2025 OnionHEN / LightningMods */

#pragma once

#include <stdint.h>

namespace onion::services {

class FtpServiceFacade {
public:
  /** Start the in-process FTP module on @port. */
  bool start(uint16_t port);
  /** Stop the current session; idempotent. */
  void stop();
  /** Apply a port change without enabling a manually disabled service. */
  bool reconfigure(uint16_t port);
  /** Rebind the listener after a network resume when FTP was enabled. */
  bool recover();
  bool running() const;
  uint16_t port() const;
};

FtpServiceFacade &ftpService();

} // namespace onion::services
