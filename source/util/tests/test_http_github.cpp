/* Host tests for onion_http_extract_commit_sha. */
#include "test_harness.h"

#include "http_github.h"

#include <cstring>

static int test_object_sha(void) {
  char sha[64];
  const char *json = R"({"sha":"abc123def456","commit":{"message":"x"}})";
  TEST_ASSERT_TRUE(onion_http_extract_commit_sha(json, sha, sizeof(sha)));
  TEST_ASSERT_STREQ("abc123def456", sha);
  return 0;
}

static int test_array_first_sha(void) {
  char sha[64];
  const char *json =
      R"([{"sha":"firstsha000","commit":{}},{"sha":"secondsha111","commit":{}}])";
  TEST_ASSERT_TRUE(onion_http_extract_commit_sha(json, sha, sizeof(sha)));
  TEST_ASSERT_STREQ("firstsha000", sha);
  return 0;
}

static int test_missing_sha(void) {
  char sha[64] = "keep";
  TEST_ASSERT_TRUE(!onion_http_extract_commit_sha(R"({"commit":{}})", sha,
                                                  sizeof(sha)));
  TEST_ASSERT_TRUE(!onion_http_extract_commit_sha(R"([])", sha, sizeof(sha)));
  TEST_ASSERT_TRUE(!onion_http_extract_commit_sha("not-json", sha, sizeof(sha)));
  return 0;
}

static int test_buffer_too_small(void) {
  char sha[4];
  TEST_ASSERT_TRUE(!onion_http_extract_commit_sha(
      R"({"sha":"toolongsha"})", sha, sizeof(sha)));
  return 0;
}

static int test_null_args(void) {
  char sha[16];
  TEST_ASSERT_TRUE(!onion_http_extract_commit_sha(nullptr, sha, sizeof(sha)));
  TEST_ASSERT_TRUE(
      !onion_http_extract_commit_sha(R"({"sha":"x"})", nullptr, 16));
  TEST_ASSERT_TRUE(!onion_http_extract_commit_sha(R"({"sha":"x"})", sha, 0));
  return 0;
}

extern "C" int test_http_github_suite(void) {
  int fails = 0;
  fails += onion_test_run("http_github.object_sha", test_object_sha);
  fails += onion_test_run("http_github.array_first", test_array_first_sha);
  fails += onion_test_run("http_github.missing_sha", test_missing_sha);
  fails += onion_test_run("http_github.buf_small", test_buffer_too_small);
  fails += onion_test_run("http_github.null_args", test_null_args);
  return fails;
}
