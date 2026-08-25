#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheats/cheat_engine_internal.h"
#include "util_platform.h"
#include "test_harness.h"
#include "test_support.h"

static int test_skip_ws(void) {
  const char text[] = "  \t\nvalue";
  const char *end = text + strlen(text);

  TEST_ASSERT_TRUE(onion_cheat_skip_ws(text, end) == text + 4);
  return 0;
}

static int test_find_matching_ignores_delimiters_inside_strings(void) {
  const char json[] = "{\"text\":\"}\",\"mods\":[1,2]}";
  const char *start = strchr(json, '{');
  const char *end = json + strlen(json);
  const char *close = onion_cheat_find_matching(start, end, '{', '}');

  TEST_ASSERT_TRUE(close != NULL);
  TEST_ASSERT_TRUE(*close == '}');
  TEST_ASSERT_TRUE((size_t)(close - start) == strlen(json) - 1);
  return 0;
}

static int test_extract_string_and_scalar(void) {
  const char json[] =
      "{\"name\":\"Infinite HP\",\"section\":1,\"offset\":\"1000\"}";
  const char *start = json;
  const char *end = json + strlen(json);
  char value[64];

  TEST_ASSERT_EQ_INT(0, onion_cheat_extract_string(start, end, "name", value,
                                                   sizeof(value)));
  TEST_ASSERT_STREQ("Infinite HP", value);
  TEST_ASSERT_EQ_INT(0, onion_cheat_extract_scalar(start, end, "section", value,
                                                   sizeof(value)));
  TEST_ASSERT_STREQ("1", value);
  TEST_ASSERT_EQ_INT(0, onion_cheat_extract_scalar(start, end, "offset", value,
                                                   sizeof(value)));
  TEST_ASSERT_STREQ("1000", value);
  TEST_ASSERT_EQ_INT(-1, onion_cheat_extract_string(start, end, "missing",
                                                    value, sizeof(value)));
  return 0;
}

static int test_hex_decode_handles_success_and_truncation(void) {
  uint8_t bytes[4];
  size_t out_len = 0;

  TEST_ASSERT_EQ_INT(0, onion_cheat_hex_decode("AABBCC", bytes, sizeof(bytes),
                                               &out_len));
  TEST_ASSERT_EQ_U64(3, out_len);
  TEST_ASSERT_EQ_INT(0xAA, bytes[0]);
  TEST_ASSERT_EQ_INT(0xBB, bytes[1]);
  TEST_ASSERT_EQ_INT(0xCC, bytes[2]);

  TEST_ASSERT_EQ_INT(0, onion_cheat_hex_decode("ABC", bytes, sizeof(bytes),
                                               &out_len));
  TEST_ASSERT_EQ_U64(2, out_len);
  TEST_ASSERT_EQ_INT(0x0A, bytes[0]);
  TEST_ASSERT_EQ_INT(0xBC, bytes[1]);

  TEST_ASSERT_EQ_INT(-1, onion_cheat_hex_decode("GZ", bytes, sizeof(bytes),
                                                &out_len));
  TEST_ASSERT_EQ_INT(-1, onion_cheat_hex_decode("AABBCC", bytes, 2, &out_len));
  TEST_ASSERT_EQ_U64(2, out_len);
  return 0;
}

static int test_replace_all_rewrites_entities(void) {
  char text[64] = "Value &lt; 10 &gt; 1";

  onion_cheat_replace_all(text, sizeof(text), "&lt;", "<");
  onion_cheat_replace_all(text, sizeof(text), "&gt;", ">");
  TEST_ASSERT_STREQ("Value < 10 > 1", text);
  return 0;
}

static int test_load_file_buffer_reads_regular_file(void) {
  char path[256];
  long size = -1;
  char *buffer = NULL;

  TEST_ASSERT_EQ_INT(
      0, onion_test_write_temp_text_file(".json", "{\"ok\":true}", path,
                                         sizeof(path)));
  buffer = onion_cheat_load_file_buffer(path, &size);
  TEST_ASSERT_TRUE(buffer != NULL);
  TEST_ASSERT_EQ_U64(strlen("{\"ok\":true}"), (size_t)size);
  TEST_ASSERT_STREQ("{\"ok\":true}", buffer);
  free(buffer);
  onion_test_remove_file(path);
  return 0;
}

static int test_load_file_buffer_rejects_empty_file(void) {
  char path[256];
  char *buffer = NULL;

  TEST_ASSERT_EQ_INT(0, onion_test_write_temp_file(".json", "", 0, path,
                                                   sizeof(path)));
  buffer = onion_cheat_load_file_buffer(path, NULL);
  TEST_ASSERT_TRUE(buffer == NULL);
  onion_test_remove_file(path);
  return 0;
}

static int test_module_section_layout(void) {
  TEST_ASSERT_EQ_U64(24, sizeof(util_module_section_t));
  TEST_ASSERT_EQ_U64(0, offsetof(util_module_section_t, vaddr));
  TEST_ASSERT_EQ_U64(8, offsetof(util_module_section_t, size));
  TEST_ASSERT_EQ_U64(16, offsetof(util_module_section_t, prot));
  TEST_ASSERT_EQ_INT(4, MODULE_INFO_MAX_SECTIONS);
  return 0;
}

int test_cheat_utils_suite(void) {
  int failures = 0;

  failures += onion_test_run("cheat utils skip ws", test_skip_ws);
  failures += onion_test_run(
      "cheat utils matching braces",
      test_find_matching_ignores_delimiters_inside_strings);
  failures +=
      onion_test_run("cheat utils extract values", test_extract_string_and_scalar);
  failures += onion_test_run("cheat utils hex decode",
                             test_hex_decode_handles_success_and_truncation);
  failures += onion_test_run("cheat utils replace all",
                             test_replace_all_rewrites_entities);
  failures += onion_test_run("cheat utils load file buffer",
                             test_load_file_buffer_reads_regular_file);
  failures += onion_test_run("cheat utils reject empty file",
                             test_load_file_buffer_rejects_empty_file);
  failures +=
      onion_test_run("module section layout", test_module_section_layout);
  return failures;
}
