/* Host tests for libonion_platform fs helpers. */
#include "test_harness.h"

#include <onion/fs.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int make_temp_dir(char *buf, size_t buflen) {
  char tmpl[] = "/tmp/onion-fs-XXXXXX";
  if (!mkdtemp(tmpl)) {
    return -1;
  }
  if (strlen(tmpl) + 1 > buflen) {
    return -1;
  }
  memcpy(buf, tmpl, strlen(tmpl) + 1);
  return 0;
}

static int test_if_exists_null_and_missing(void) {
  TEST_ASSERT_TRUE(!if_exists(NULL));
  TEST_ASSERT_TRUE(!if_exists("/tmp/onion-fs-definitely-missing-xyz-9f3a"));
  return 0;
}

static int test_touch_and_exists(void) {
  char dir[64];
  char path[128];
  TEST_ASSERT_EQ_INT(0, make_temp_dir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/flag", dir);

  TEST_ASSERT_TRUE(!if_exists(path));
  TEST_ASSERT_TRUE(touch_file(path));
  TEST_ASSERT_TRUE(if_exists(path));
  TEST_ASSERT_TRUE(touch_file(path)); /* truncate ok */

  TEST_ASSERT_TRUE(!touch_file(NULL));

  unlink(path);
  rmdir(dir);
  return 0;
}

static int test_rmtree_nested(void) {
  char dir[64];
  char sub[128];
  char file[160];
  TEST_ASSERT_EQ_INT(0, make_temp_dir(dir, sizeof(dir)));
  snprintf(sub, sizeof(sub), "%s/nested", dir);
  TEST_ASSERT_EQ_INT(0, mkdir(sub, 0777));
  snprintf(file, sizeof(file), "%s/a.txt", sub);
  TEST_ASSERT_TRUE(touch_file(file));
  snprintf(file, sizeof(file), "%s/b.txt", dir);
  TEST_ASSERT_TRUE(touch_file(file));

  TEST_ASSERT_TRUE(rmtree(dir));
  TEST_ASSERT_TRUE(!if_exists(dir));
  TEST_ASSERT_TRUE(!rmtree(NULL));
  TEST_ASSERT_TRUE(!rmtree("/tmp/onion-fs-missing-tree-xyz"));
  return 0;
}

int test_platform_fs_suite(void) {
  int failures = 0;
  failures += onion_test_run("fs_if_exists_null_missing", test_if_exists_null_and_missing);
  failures += onion_test_run("fs_touch_and_exists", test_touch_and_exists);
  failures += onion_test_run("fs_rmtree_nested", test_rmtree_nested);
  return failures;
}
