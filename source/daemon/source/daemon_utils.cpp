/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Shared daemon helpers (file/net/app query) — extracted from commands.cpp.
 */

#include "daemon_ops.hpp"
#include "launcher.hpp"

#include <onion/platform.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

extern "C" {
  int sceUserServiceGetLoginUserIdList(void *list);
  int sceUserServiceGetUserName(const int userId, char *userName, const size_t size);
  int sceSystemServiceGetAppIdOfRunningBigApp();
  int sceSystemServiceGetAppTitleId(int app_id, char *title_id);
}

namespace {

struct UserServiceLoginUserIdList {
  int user_id[4];
};

} // namespace

bool GetFileContents(const char *path, char **buffer) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    LOG_ERROR("failed to open %s", path);
    return false;
  }

  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size == 0) {
    fclose(fp);
    LOG_INFO("size is 0");
    return false;
  }

  *buffer = (char *)malloc(size + 1);
  if (*buffer == NULL) {
    LOG_ERROR("failed to allocate memory (OOM)");
    fclose(fp);
    return false;
  }

  if (fread(*buffer, size, 1, fp) != 1) {
    fclose(fp);
    free(*buffer);
    return false;
  }

  fclose(fp);
  (*buffer)[size] = '\0';
  return true;
}

bool Get_Running_App_TID(std::string &title_id, int &BigAppid) {
  char tid[255];
  BigAppid = sceSystemServiceGetAppIdOfRunningBigApp();
  if (BigAppid < 0)
    return false;

  (void)memset(tid, 0, sizeof tid);
  if (sceSystemServiceGetAppTitleId(BigAppid, &tid[0]) != 0)
    return false;

  title_id = std::string(tid);
  return true;
}

bool isUserLoggedIn() {
  bool isLoggedIn = false;
  UserServiceLoginUserIdList userIdList{};
  (void)memset(&userIdList, 0, sizeof(userIdList));

  if (sceUserServiceGetLoginUserIdList(&userIdList) < 0)
    return false;

  for (int i = 0; i < 4; i++) {
    char username[500] = {0};
    int userid = userIdList.user_id[i];
    if (userid == -1)
      continue;
    int ret = sceUserServiceGetUserName(userid, &username[0], sizeof(username));
    LOG_INFO("sceUserServiceGetUserName returned %d", ret);
    if (ret == 0) {
      isLoggedIn = true;
      break;
    }
  }

  sleep(5);
  return isLoggedIn;
}

bool Open_Utility_Elf(const char *path, uint8_t **buffer) {
  if (!path || !buffer) {
    LOG_ERROR("Invalid arguments: path or buffer is null.");
    return false;
  }

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    LOG_ERROR("Failed to open file: %s (error: %s)", path, strerror(errno));
    return false;
  }

  struct stat st;
  if (fstat(fd, &st) != 0) {
    LOG_ERROR("Failed to get file stats for %s (error: %s)", path, strerror(errno));
    close(fd);
    return false;
  }

  if (st.st_size == 0) {
    LOG_INFO("File %s is empty.", path);
    close(fd);
    return false;
  }

  uint8_t *buf = (uint8_t *)malloc((size_t)st.st_size);
  if (!buf) {
    LOG_ERROR("Failed to allocate memory for file %s (size: %ld bytes).", path,
                 st.st_size);
    close(fd);
    return false;
  }

  ssize_t bytes_read = read(fd, buf, (size_t)st.st_size);
  if (bytes_read != st.st_size) {
    LOG_ERROR("Failed to read the entire file %s (read: %ld bytes, expected: %ld bytes).",
                 path, bytes_read, st.st_size);
    free(buf);
    close(fd);
    return false;
  }

  close(fd);
  *buffer = buf;
  return true;
}
