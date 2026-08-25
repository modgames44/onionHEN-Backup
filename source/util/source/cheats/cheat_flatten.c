#include <onion/log.h>
#include "cheats/runtime.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const k_cheat_extensions[] = {"json", "shn", "mc4",
                                                 "ShnExt"};

const char *onion_cheat_extension_for_rank(int rank) {
  if (rank < 0 || (size_t)rank >= sizeof(k_cheat_extensions) /
                                      sizeof(k_cheat_extensions[0])) {
    return NULL;
  }
  return k_cheat_extensions[rank];
}

int onion_cheat_extension_rank(const char *name, size_t *extension_start) {
  size_t name_len;

  if (name == NULL) {
    return -1;
  }
  name_len = strlen(name);
  for (size_t i = 0;
       i < sizeof(k_cheat_extensions) / sizeof(k_cheat_extensions[0]); ++i) {
    const char *extension = k_cheat_extensions[i];
    const size_t extension_len = strlen(extension);
    if (name_len <= extension_len + 1 ||
        name[name_len - extension_len - 1] != '.') {
      continue;
    }
    if (strcasecmp(name + name_len - extension_len, extension) == 0) {
      if (extension_start != NULL) {
        *extension_start = name_len - extension_len - 1;
      }
      return (int)i;
    }
  }
  return -1;
}

int onion_cheat_match_ext(const char *name, char *ext_out, size_t ext_out_size) {
  const int rank = onion_cheat_extension_rank(name, NULL);
  const char *extension;
  if (rank < 0) {
    return 0;
  }
  extension = onion_cheat_extension_for_rank(rank);
  if (ext_out != NULL && ext_out_size > 0) {
    snprintf(ext_out, ext_out_size, "%s", extension);
  }
  return 1;
}

static int ascii_iequals(const char *lhs, const char *rhs) {
  if (lhs == NULL || rhs == NULL) {
    return 0;
  }
  return strcasecmp(lhs, rhs) == 0;
}

static int ascii_icontains(const char *value, const char *needle) {
  size_t value_len;
  size_t needle_len;
  size_t i;

  if (value == NULL || needle == NULL || needle[0] == '\0') {
    return 0;
  }
  value_len = strlen(value);
  needle_len = strlen(needle);
  if (value_len < needle_len) {
    return 0;
  }
  for (i = 0; i + needle_len <= value_len; ++i) {
    if (strncasecmp(value + i, needle, needle_len) == 0) {
      return 1;
    }
  }
  return 0;
}

int onion_cheat_parse_filename(const char *filename,
                               onion_cheat_filename_t *out) {
  char base[256];
  size_t extension_start = 0;
  const char *sep;
  const char *vstart;
  const char *vend;
  size_t title_len;
  size_t version_len;
  size_t suffix_len;
  size_t i;

  if (filename == NULL || out == NULL) {
    return -1;
  }
  memset(out, 0, sizeof(*out));
  out->extension_rank = onion_cheat_extension_rank(filename, &extension_start);
  if (out->extension_rank < 0 || extension_start == 0 ||
      extension_start >= sizeof(base)) {
    return -1;
  }

  memcpy(base, filename, extension_start);
  base[extension_start] = '\0';
  if (extension_start >= 4 &&
      strcasecmp(base + extension_start - 4, ".xml") == 0) {
    base[extension_start - 4] = '\0';
  }

  sep = base;
  while (*sep && *sep != '_' && *sep != '-' && *sep != ' ') {
    ++sep;
  }
  title_len = (size_t)(sep - base);
  if (*sep == '\0' || title_len < 4 ||
      title_len >= sizeof(out->title_id)) {
    return -1;
  }
  memcpy(out->title_id, base, title_len);
  out->title_id[title_len] = '\0';

  vstart = sep + 1;
  vend = vstart;
  while (*vend &&
         ((*vend >= '0' && *vend <= '9') || *vend == '.' || *vend == 'x' ||
          *vend == 'X')) {
    ++vend;
  }
  version_len = (size_t)(vend - vstart);
  if (version_len == 0 || version_len >= sizeof(out->version)) {
    return -1;
  }
  memcpy(out->version, vstart, version_len);
  out->version[version_len] = '\0';

  if (*vend == '_' || *vend == '-' || *vend == ' ') {
    ++vend;
  }
  suffix_len = strlen(vend);
  if (suffix_len >= sizeof(out->suffix)) {
    return -1;
  }
  memcpy(out->suffix, vend, suffix_len + 1);

  for (i = 0; out->title_id[i]; ++i) {
    out->title_id[i] = (char)toupper((unsigned char)out->title_id[i]);
  }
  return 0;
}

int onion_cheat_is_legacy_eboot_alias(const char *suffix) {
  if (suffix == NULL || suffix[0] == '\0') {
    return 0;
  }
  if (ascii_icontains(suffix, ".bin") && !ascii_iequals(suffix, "eboot.bin")) {
    return 0;
  }
  return 1;
}

int onion_cheat_build_flat_name(const char *filename, char *out, size_t out_size) {
  onion_cheat_filename_t parts;
  const char *extension;

  if (out == NULL || out_size == 0) {
    return -1;
  }
  if (onion_cheat_parse_filename(filename, &parts) < 0) {
    return -1;
  }
  extension = onion_cheat_extension_for_rank(parts.extension_rank);
  if (extension == NULL) {
    return -1;
  }
  snprintf(out, out_size, "%s_%s.%s", parts.title_id, parts.version, extension);
  return 0;
}

static int copy_file(const char *src, const char *dst) {
  FILE *in = fopen(src, "rb");
  FILE *out = NULL;
  char buf[8192];
  size_t n;

  if (in == NULL) {
    return -1;
  }
  out = fopen(dst, "wb");
  if (out == NULL) {
    fclose(in);
    return -1;
  }
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      fclose(in);
      fclose(out);
      return -1;
    }
  }
  fclose(in);
  fclose(out);
  return 0;
}

static void walk_and_flatten(const char *dir, int *copied, int *skipped) {
  DIR *d = opendir(dir);
  struct dirent *ent;

  if (d == NULL) {
    return;
  }
  while ((ent = readdir(d)) != NULL) {
    char path[512];
    char flat[256];
    char dest[512];
    struct stat st;

    if (ent->d_name[0] == '.') {
      continue;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
    if (stat(path, &st) != 0) {
      continue;
    }
    if (S_ISDIR(st.st_mode)) {
      walk_and_flatten(path, copied, skipped);
      continue;
    }
    if (!S_ISREG(st.st_mode)) {
      continue;
    }
    if (onion_cheat_build_flat_name(ent->d_name, flat, sizeof(flat)) < 0) {
      continue;
    }
    snprintf(dest, sizeof(dest), ONION_CHEATS_DIR "/%s", flat);
    if (strcmp(path, dest) == 0) {
      continue;
    }
    if (copy_file(path, dest) == 0) {
      (*copied)++;
      LOG_INFO("[flatten] %s -> %s", path, dest);
    } else {
      (*skipped)++;
    }
  }
  closedir(d);
}

/**
 * Walk a tree (typically after zip extract) and install flat cheat files into
 * ONION_CHEATS_DIR as <TITLE_ID>_<VERSION>.<ext>.
 */
void onion_cheat_normalize_filename_token(const char *value, char *out,
                                          size_t out_size) {
  size_t j = 0;
  if (out == NULL || out_size == 0)
    return;
  out[0] = '\0';
  if (!value)
    return;
  for (size_t i = 0; value[i] && j + 1 < out_size; ++i) {
    const unsigned char ch = (unsigned char)value[i];
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') || ch == '.' || ch == '_' || ch == '-') {
      out[j++] = (char)ch;
    } else {
      out[j++] = '_';
    }
  }
  out[j] = '\0';
}

void onion_cheat_normalize_version(const char *version, char *out,
                                   size_t out_size) {
  onion_cheat_normalize_filename_token(version, out, out_size);
}

int onion_cheat_flatten_install_tree(const char *root) {
  int copied = 0;
  int skipped = 0;

  mkdir(ONION_DATA_ROOT, 0777);
  mkdir(ONION_CHEATS_DIR, 0777);

  if (root == NULL || root[0] == '\0') {
    root = ONION_CHEATS_DIR;
  }
  walk_and_flatten(root, &copied, &skipped);
  LOG_WARN("[flatten] installed %d cheat file(s), skipped %d", copied,
                   skipped);
  return copied > 0 ? 0 : -1;
}
