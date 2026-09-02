
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

#include "ipc.hpp"
#include <onion/ipc_server.hpp>
#include <msg.hpp>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>
#include <atomic>
extern "C" {
#include "common_utils.h"
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>

int sceKernelMprotect(void *addr, size_t len, int prot);


int sceSystemServiceLoadExec(const char *path, const char *arg);
extern bool is_handler_enabled;
}
#include "onion_cjson.hpp"
#include "cheats/cheat_service.hpp"
#include "cheats/runtime.h"
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <fstream>
#include <memory>
#include <sfo.hpp>
#include <sstream>
#include <onion/platform.h>

// pop -Winfinite-recursion error for this func for clang
#define MB(x) ((size_t)(x) << 20)
#define READ_SIZE 0x1024

extern int shellui_pid_for_comp;
extern uintptr_t code_addr;


extern "C" int launchApp(const char *titleId);

void activate_shellui_patch();


struct sockaddr_in networkAdress(uint16_t port) {
  struct sockaddr_in address;
  address.sin_len = sizeof(address);
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  memset(address.sin_zero, 0, sizeof(address.sin_zero));
  return address;
}

void reply(int sender_socket, bool error, std::string out_var = "Nothing") {
  onion::ipc_reply(sender_socket, BREW_UTIL_RETURN_VALUE, error, out_var);
}

std::vector<uint8_t> readFile(std::string filename) {
  // open the file:
  std::ifstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    LOG_ERROR("Failed to open %s", filename.c_str());
    return std::vector<uint8_t>();
  }

  // Stop eating new lines in binary mode!!!
  file.unsetf(std::ios::skipws);

  // get its size:
  std::streampos fileSize;

  file.seekg(0, std::ios::end);
  fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  // reserve capacity
  std::vector<uint8_t> vec;

  vec.reserve(fileSize);

  // read the data:
  vec.insert(vec.begin(), std::istream_iterator<uint8_t>(file),
             std::istream_iterator<uint8_t>());

  return vec;
}

std::string GetPS5Version(const std::string &jsonpath) {
  try {
    std::ifstream input_file(jsonpath);
    if (!input_file.is_open()) {
      LOG_ERROR("Failed to open file for reading: %s", jsonpath.c_str());
      return "Error Opening Json";
    }

    std::stringstream buffer;
    buffer << input_file.rdbuf();
    input_file.close();

    onion_cjson::Root j(buffer.str());
    const char *content_version =
        j ? onion_cjson::string_item(j.get(), "contentVersion") : nullptr;
    if (content_version)
      return std::string(content_version);

  } catch (const std::exception &e) {
    // Handle exceptions here, you can log the error or perform other error
    // handling tasks
    LOG_ERROR("Failed to read game version: %s", e.what());
    return "Error getting version";
  }

  return "Error getting version";
}

// Callback function to write received data
size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
  FILE *fp = (FILE *)userp;
  return fwrite(contents, size, nmemb, fp);
}

/* handleIPC → ipc_handle.cpp */
void handleIPC(clientArgs *client, std::string &inputStr,
               DaemonCommands command);

static void handleIPC_adapt(onion::IpcClientArgs *client, std::string &msg,
                            DaemonCommands cmd) {
  handleIPC(client, msg, cmd);
}

static std::atomic_bool g_util_ipc_running{true};
static std::atomic<int> g_util_ipc_server_fd{-1};

static onion::IpcServerOptions g_util_ipc_opts = {
    UTIL_IPC_SOC,
    handleIPC_adapt,
    BREW_UTIL_RETURN_VALUE,
    true,
    "util",
    &g_util_ipc_running,
    &g_util_ipc_server_fd,
};

void *IPC_loop(void *args) {
  (void)args;
  return onion::ipc_server_loop(&g_util_ipc_opts);
}
