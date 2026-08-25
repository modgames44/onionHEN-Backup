/* Copyright (C) 2025 OnionHEN / LightningMods */

#include <onion/fs.h>
#include <onion/log.h>

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

bool if_exists(const char *path) {
  struct stat buffer;
  if (!path) {
    return false;
  }
  return stat(path, &buffer) == 0;
}

bool touch_file(const char *path) {
  if (!path) {
    return false;
  }
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if (fd > 0) {
    close(fd);
    return true;
  }
  return false;
}

bool rmtree(const char *path) {
  if (!path) {
    return false;
  }

  DIR *dir = opendir(path);
  if (dir == NULL) {
    LOG_ERROR("Error opening directory %s", path);
    return false;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char child[1024];
    snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);

    if (entry->d_type == DT_DIR) {
      rmtree(child);
    } else if (unlink(child) != 0) {
      LOG_ERROR("Error deleting file %s", child);
    }
  }

  closedir(dir);

  if (rmdir(path) != 0) {
    LOG_ERROR("Error deleting folder %s", path);
  }
  return true;
}
