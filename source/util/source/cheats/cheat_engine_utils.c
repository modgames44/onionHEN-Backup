#include <onion/log.h>
#include "cheats/cheat_engine_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util_platform.h"


/**
 * 跳过字符串中的空白字符。
 *
 * @param p 当前指针位置。
 * @param end 字符串结束指针。
 * @return 跳过空白后的新指针位置。
 */
const char *onion_cheat_skip_ws(const char *p, const char *end) {
  while (p < end && isspace((unsigned char)*p)) {
    ++p;
  }
  return p;
}

/**
 * 在 JSON 数据中查找指定键的位置。
 * 返回键名后的冒号位置指针。
 *
 * @param start 搜索起始位置。
 * @param end 搜索结束位置。
 * @param key 待查找的键名。
 * @return 找到时返回键值对中冒号后的位置，未找到返回 NULL。
 */
const char *onion_cheat_find_key(const char *start, const char *end,
                                 const char *key) {
  char pattern[64];
  const char *p = start;

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  while ((p = strstr(p, pattern)) != NULL) {
    if (p >= end) {
      return NULL;
    }
    p += strlen(pattern);
    p = onion_cheat_skip_ws(p, end);
    if (p < end && *p == ':') {
      return p + 1;
    }
  }

  return NULL;
}

/**
 * 查找与起始括号匹配的结束括号位置。
 * 支持嵌套括号和字符串转义。
 *
 * @param start 搜索起始位置。
 * @param end 搜索结束位置。
 * @param open_ch 起始括号字符。
 * @param close_ch 结束括号字符。
 * @return 匹配的结束括号位置，未找到返回 NULL。
 */
const char *onion_cheat_find_matching(const char *start, const char *end,
                                      char open_ch, char close_ch) {
  int depth = 0;
  bool in_string = false;

  for (const char *p = start; p < end; ++p) {
    if (*p == '"' && (p == start || p[-1] != '\\')) {
      in_string = !in_string;
      continue;
    }
    if (in_string) {
      continue;
    }
    if (*p == open_ch) {
      ++depth;
    } else if (*p == close_ch) {
      --depth;
      if (depth == 0) {
        return p;
      }
    }
  }

  return NULL;
}

/**
 * 从 JSON 数据中提取指定键的字符串值。
 * 自动处理引号和转义字符。
 *
 * @param start JSON 数据的起始位置。
 * @param end JSON 数据的结束位置。
 * @param key 待提取的键名。
 * @param out 输出缓冲区，用于存储提取的字符串值。
 * @param out_size 输出缓冲区的大小。
 * @return 成功返回 0，失败返回 -1。
 */
int onion_cheat_extract_string(const char *start, const char *end,
                               const char *key, char *out, size_t out_size) {
  const char *p = onion_cheat_find_key(start, end, key);
  const char *q = NULL;
  size_t len = 0;

  out[0] = '\0';
  if (p == NULL) {
    return -1;
  }

  p = onion_cheat_skip_ws(p, end);
  if (p >= end || *p != '"') {
    return -1;
  }

  ++p;
  q = p;
  while (q < end) {
    if (*q == '"' && q[-1] != '\\') {
      break;
    }
    ++q;
  }
  if (q >= end) {
    return -1;
  }

  len = (size_t)(q - p);
  if (len >= out_size) {
    len = out_size - 1;
  }
  memcpy(out, p, len);
  out[len] = '\0';
  return 0;
}

/**
 * 从 JSON 数据中提取指定键的标量值（数字或字符串）。
 * 自动检测值类型，字符串值会委托给 extract_string 处理。
 *
 * @param start JSON 数据的起始位置。
 * @param end JSON 数据的结束位置。
 * @param key 待提取的键名。
 * @param out 输出缓冲区，用于存储提取的标量值字符串。
 * @param out_size 输出缓冲区的大小。
 * @return 成功返回 0，失败返回 -1。
 */
int onion_cheat_extract_scalar(const char *start, const char *end,
                               const char *key, char *out, size_t out_size) {
  const char *p = onion_cheat_find_key(start, end, key);
  const char *q = NULL;
  size_t len = 0;

  out[0] = '\0';
  if (p == NULL) {
    return -1;
  }

  p = onion_cheat_skip_ws(p, end);
  if (p >= end) {
    return -1;
  }

  if (*p == '"') {
    return onion_cheat_extract_string(start, end, key, out, out_size);
  }

  q = p;
  while (q < end && *q != ',' && *q != '}' && *q != ']' &&
         !isspace((unsigned char)*q)) {
    ++q;
  }

  len = (size_t)(q - p);
  if (len >= out_size) {
    len = out_size - 1;
  }
  memcpy(out, p, len);
  out[len] = '\0';
  return 0;
}

/**
 * 将十六进制字符串解码为字节数组。
 * 支持奇数长度（首字节自动补 0）。
 *
 * @param hex 输入的十六进制字符串。
 * @param out 输出缓冲区，用于存储解码后的字节数据。
 * @param max_len 输出缓冲区的最大长度。
 * @param out_len 输出参数，实际解码的字节数。
 * @return 成功返回 0，失败返回 -1。
 */
int onion_cheat_hex_decode(const char *hex, uint8_t *out, size_t max_len,
                           size_t *out_len) {
  size_t len = strlen(hex);
  size_t i = 0;
  size_t j = 0;
  char tmp[3];

  tmp[2] = '\0';
  if ((len % 2) != 0) {
    if (max_len == 0 || !isxdigit((unsigned char)hex[0])) {
      return -1;
    }
    tmp[0] = '0';
    tmp[1] = hex[i++];
    out[j++] = (uint8_t)strtoul(tmp, NULL, 16);
  }

  while (i < len && j < max_len) {
    tmp[0] = hex[i++];
    tmp[1] = hex[i++];
    if (!isxdigit((unsigned char)tmp[0]) ||
        !isxdigit((unsigned char)tmp[1])) {
      return -1;
    }
    out[j++] = (uint8_t)strtoul(tmp, NULL, 16);
  }

  *out_len = j;
  return i == len ? 0 : -1;
}

/**
 * 从文件系统加载文件内容到缓冲区。
 * 文件大小限制为 long 类型正数范围内。
 *
 * @param path 文件路径。
 * @param size_out 输出参数，文件大小（字节）。
 * @return 指向文件内容的缓冲区指针（需调用者 free），失败返回 NULL。
 */
char *onion_cheat_load_file_buffer(const char *path, long *size_out) {
  char *buf = NULL;
  size_t buf_size = 0;

  LOG_INFO("[engine] load_file_buffer path=%s",
                   path ? path : "(null)");
  if (util_file_read_alloc(path, &buf, &buf_size, (size_t)-1) < 0) {
    LOG_ERROR("[engine] load_file_buffer failed path=%s", path);
    return NULL;
  }
  if (size_out != NULL) {
    *size_out = (long)buf_size;
  }
  LOG_INFO("[engine] load_file_buffer ok size=%zu",
                   buf_size);
  return buf;
}

/**
 * 在字符串中原地替换所有匹配的子串。
 * 使用临时缓冲区避免重叠替换问题。
 *
 * @param text 待处理的字符串（将被原地修改）。
 * @param cap 输出缓冲区的容量。
 * @param from 待替换的源子串。
 * @param to 替换后的目标子串。
 * @return 无返回值。
 */
void onion_cheat_replace_all(char *text, size_t cap, const char *from,
                             const char *to) {
  size_t text_len = strlen(text);
  size_t tmp_cap = (text_len + 1) * 4;
  char *tmp = NULL;
  char *dst = NULL;
  const char *src = text;
  size_t from_len = strlen(from);
  size_t to_len = strlen(to);

  if (tmp_cap < cap) {
    tmp_cap = cap;
  }
  tmp = (char *)calloc(tmp_cap, 1);
  if (tmp == NULL) {
    return;
  }
  dst = tmp;

  while (*src != '\0' && (size_t)(dst - tmp) + 1 < tmp_cap) {
    if (strncmp(src, from, from_len) == 0) {
      if ((size_t)(dst - tmp) + to_len + 1 >= tmp_cap) {
        break;
      }
      memcpy(dst, to, to_len);
      dst += to_len;
      src += from_len;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
  snprintf(text, cap, "%s", tmp);
  onion_cheat_secure_zero(tmp, tmp_cap);
  free(tmp);
}

void onion_cheat_secure_zero(void *ptr, size_t len) {
  volatile unsigned char *p = (volatile unsigned char *)ptr;

  if (p == NULL) {
    return;
  }
  while (len-- > 0) {
    *p++ = 0;
  }
}

void onion_cheat_file_clear(onion_cheat_file_t *f) {
  if (f == NULL) return;
  for (size_t i = 0; i < f->cheat_count; i++) {
    if (f->cheats[i].patches != NULL) {
      onion_cheat_secure_zero(f->cheats[i].patches,
                              f->cheats[i].patch_capacity *
                                  sizeof(onion_patch_t));
    }
    free(f->cheats[i].patches);
  }
  if (f->cheats != NULL) {
    onion_cheat_secure_zero(f->cheats,
                            f->cheat_capacity * sizeof(onion_cheat_entry_t));
  }
  free(f->cheats);
  memset(f, 0, sizeof(*f));
  f->master_code_id = -1;
}

int onion_cheat_file_add_author(onion_cheat_file_t *f, const char *author) {
  size_t i = 0;

  if (f == NULL || author == NULL || author[0] == '\0') {
    return -1;
  }
  for (i = 0; i < f->author_count; ++i) {
    if (strcmp(f->authors[i], author) == 0) {
      return 0;
    }
  }
  if (f->author_count >= ONION_MAX_AUTHORS) {
    return -1;
  }
  snprintf(f->authors[f->author_count], ONION_AUTHOR_NAME_LEN, "%s", author);
  ++f->author_count;
  return 0;
}

int onion_cheat_file_ensure_cheat(onion_cheat_file_t *f) {
  if (f == NULL) return -1;
  if (f->cheat_count < f->cheat_capacity) return 0;
  size_t new_cap = f->cheat_capacity == 0 ? 4 : f->cheat_capacity * 2;
  onion_cheat_entry_t *new_arr = realloc(f->cheats, new_cap * sizeof(onion_cheat_entry_t));
  if (new_arr == NULL) return -1;
  memset(&new_arr[f->cheat_capacity], 0,
         (new_cap - f->cheat_capacity) * sizeof(onion_cheat_entry_t));
  f->cheats = new_arr;
  f->cheat_capacity = new_cap;
  return 0;
}

int onion_cheat_entry_ensure_patch(onion_cheat_entry_t *e) {
  if (e == NULL) return -1;
  if (e->patch_count < e->patch_capacity) return 0;
  size_t new_cap = e->patch_capacity == 0 ? 4 : e->patch_capacity * 2;
  onion_patch_t *new_arr = realloc(e->patches, new_cap * sizeof(onion_patch_t));
  if (new_arr == NULL) return -1;
  memset(&new_arr[e->patch_capacity], 0,
         (new_cap - e->patch_capacity) * sizeof(onion_patch_t));
  e->patches = new_arr;
  e->patch_capacity = new_cap;
  return 0;
}
