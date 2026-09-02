#include "test_harness.h"

#include "util_language.h"

#include <onion/notify_i18n.h>

void onion_test_system_language_configure(int result, int value);

static int test_refresh_preserves_last_valid_language(void) {
  onion_test_system_language_configure(0, 11);
  TEST_ASSERT_TRUE(util_refresh_system_language());
  TEST_ASSERT_EQ_INT(11, util_cached_system_language());
  util_apply_ui_language(0);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ZH_HANS, onion_notify_get_language());

  onion_test_system_language_configure(-1, 1);
  TEST_ASSERT_TRUE(!util_refresh_system_language());
  TEST_ASSERT_EQ_INT(11, util_cached_system_language());
  util_apply_ui_language(0);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ZH_HANS, onion_notify_get_language());

  onion_test_system_language_configure(0, 1);
  TEST_ASSERT_TRUE(util_refresh_system_language());
  TEST_ASSERT_EQ_INT(1, util_cached_system_language());
  util_apply_ui_language(0);
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_EN, onion_notify_get_language());
  return 0;
}

int test_util_language_suite(void) {
  return onion_test_run("util_language.refresh_preserves_last_valid",
                        test_refresh_preserves_last_valid_language);
}
