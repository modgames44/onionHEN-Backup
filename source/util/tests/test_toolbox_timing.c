/* Host tests: toolbox rest-delay policy (cold start vs rest resume). */
#include "test_harness.h"

#include <onion/toolbox_timing.h>
#include <onion/ready.h>

#include <stdio.h>
#include <unistd.h>

static int test_cold_start_no_delay(void) {
  /* util_booted alone must not imply rest delay. */
  TEST_ASSERT_TRUE(!onion_toolbox_should_apply_rest_delay(false, 30));
  TEST_ASSERT_TRUE(!onion_toolbox_should_apply_rest_delay(false, 0));
  return 0;
}

static int test_rest_resume_applies_delay(void) {
  TEST_ASSERT_TRUE(onion_toolbox_should_apply_rest_delay(true, 5));
  TEST_ASSERT_TRUE(!onion_toolbox_should_apply_rest_delay(true, 0));
  return 0;
}

static int test_util_booted_flag_does_not_force_delay(void) {
  /* Simulate cold-start ordering: util signals util_booted before daemon inject. */
  char name[64];
  snprintf(name, sizeof(name), "tb_timing_%d", (int)getpid());
  onion_ready_clear(name);
  /* Use a private flag name — ready API rejects dots/slashes; ok for smoke. */
  (void)onion_ready_signal(ONION_FLAG_UTIL_BOOTED);
  /* Policy still requires rest_resume=true; util_booted is orthogonal. */
  TEST_ASSERT_TRUE(
      !onion_toolbox_should_apply_rest_delay(/*rest_resume=*/false, 10));
  return 0;
}

int test_toolbox_timing_suite(void) {
  int failures = 0;
  failures += onion_test_run("toolbox_timing.cold_no_delay", test_cold_start_no_delay);
  failures +=
      onion_test_run("toolbox_timing.rest_applies", test_rest_resume_applies_delay);
  failures += onion_test_run("toolbox_timing.util_booted_orthogonal",
                             test_util_booted_flag_does_not_force_delay);
  return failures;
}
