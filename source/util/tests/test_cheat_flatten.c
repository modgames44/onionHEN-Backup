/* Host unit tests for cheat flat-name helpers (no PS5 FS required). */
#include "test_harness.h"

#include "cheats/runtime.h"

#include <string.h>

static int test_match_ext_known(void) {
  char ext[16];
  size_t extension_start = 0;

  TEST_ASSERT_EQ_INT(1, onion_cheat_match_ext("CUSA12345_01.00.json", ext,
                                              sizeof(ext)));
  TEST_ASSERT_STREQ("json", ext);

  TEST_ASSERT_EQ_INT(1, onion_cheat_match_ext("PPSA00001_01.001.000.SHN", ext,
                                              sizeof(ext)));
  TEST_ASSERT_STREQ("shn", ext);

  TEST_ASSERT_EQ_INT(1, onion_cheat_match_ext("game.mc4", ext, sizeof(ext)));
  TEST_ASSERT_STREQ("mc4", ext);

  TEST_ASSERT_EQ_INT(1, onion_cheat_match_ext("x.ShnExt", ext, sizeof(ext)));
  TEST_ASSERT_STREQ("ShnExt", ext);

  TEST_ASSERT_EQ_INT(1, onion_cheat_match_ext("x.shnext", ext, sizeof(ext)));
  TEST_ASSERT_STREQ("ShnExt", ext);

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_extension_rank("CUSA12345_01.00.JSON", &extension_start));
  TEST_ASSERT_EQ_INT(15, (int)extension_start);
  TEST_ASSERT_STREQ("json", onion_cheat_extension_for_rank(0));
  TEST_ASSERT_STREQ("ShnExt", onion_cheat_extension_for_rank(3));
  TEST_ASSERT_TRUE(onion_cheat_extension_for_rank(-1) == NULL);
  TEST_ASSERT_TRUE(onion_cheat_extension_for_rank(4) == NULL);
  return 0;
}

static int test_match_ext_reject(void) {
  char ext[16] = "keep";

  TEST_ASSERT_EQ_INT(0, onion_cheat_match_ext("readme.txt", ext, sizeof(ext)));
  TEST_ASSERT_EQ_INT(0, onion_cheat_match_ext("json", ext, sizeof(ext)));
  TEST_ASSERT_EQ_INT(0, onion_cheat_match_ext(NULL, ext, sizeof(ext)));
  TEST_ASSERT_EQ_INT(0, onion_cheat_match_ext("", ext, sizeof(ext)));
  TEST_ASSERT_EQ_INT(0, onion_cheat_match_ext(".json", ext, sizeof(ext)));
  return 0;
}

static int test_flat_simple(void) {
  char out[128];

  TEST_ASSERT_EQ_INT(0, onion_cheat_build_flat_name("CUSA05786_01.04.json", out,
                                                   sizeof(out)));
  TEST_ASSERT_STREQ("CUSA05786_01.04.json", out);

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_build_flat_name("PPSA01340_01.004.000.shn", out,
                                    sizeof(out)));
  TEST_ASSERT_STREQ("PPSA01340_01.004.000.shn", out);
  return 0;
}

static int test_flat_strips_process_suffix(void) {
  char out[128];

  /* GoldHEN style: TITLE_VER_process.json → drop process segment */
  TEST_ASSERT_EQ_INT(
      0, onion_cheat_build_flat_name("CUSA05786_01.04_eboot.bin.json", out,
                                    sizeof(out)));
  TEST_ASSERT_STREQ("CUSA05786_01.04.json", out);

  /* Website-specific third field: keep TITLE/VERSION, discard the alias. */
  TEST_ASSERT_EQ_INT(
      0, onion_cheat_build_flat_name(
             "PPSA17168_01.004.000_97905f51.json", out, sizeof(out)));
  TEST_ASSERT_STREQ("PPSA17168_01.004.000.json", out);

  /* Game name before first '_' with non-version text → no digit version */
  TEST_ASSERT_EQ_INT(
      -1, onion_cheat_build_flat_name(
              "Assassins-Creed-Mirage_PPSA07230_01.012.000.ShnExt", out,
              sizeof(out)));
  return 0;
}

static int test_flat_shnext_with_tid_prefix(void) {
  char out[128];

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_build_flat_name("PPSA07230_01.012.000_Aigars_Uze.ShnExt",
                                    out, sizeof(out)));
  TEST_ASSERT_STREQ("PPSA07230_01.012.000.ShnExt", out);
  return 0;
}

static int test_flat_uppercase_title(void) {
  char out[128];

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_build_flat_name("cusa12345_1.00.json", out, sizeof(out)));
  TEST_ASSERT_STREQ("CUSA12345_1.00.json", out);
  return 0;
}

static int test_flat_dash_separator(void) {
  char out[128];

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_build_flat_name("CUSA12345-01.00.json", out, sizeof(out)));
  TEST_ASSERT_STREQ("CUSA12345_01.00.json", out);
  return 0;
}

static int test_flat_reject(void) {
  char out[128];

  TEST_ASSERT_EQ_INT(-1, onion_cheat_build_flat_name("readme.txt", out,
                                                    sizeof(out)));
  TEST_ASSERT_EQ_INT(-1, onion_cheat_build_flat_name("nounderscore.json", out,
                                                    sizeof(out)));
  TEST_ASSERT_EQ_INT(-1, onion_cheat_build_flat_name("ab_01.json", out,
                                                    sizeof(out))); /* tid < 4 */
  TEST_ASSERT_EQ_INT(-1, onion_cheat_build_flat_name("CUSA12345_.json", out,
                                                    sizeof(out))); /* no ver */
  TEST_ASSERT_EQ_INT(-1, onion_cheat_build_flat_name(NULL, out, sizeof(out)));
  TEST_ASSERT_EQ_INT(-1, onion_cheat_build_flat_name("CUSA12345_1.0.json", NULL,
                                                    0));
  return 0;
}

static int test_parse_filename_parts(void) {
  onion_cheat_filename_t parts;

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_parse_filename("CUSA05786_01.04.json", &parts));
  TEST_ASSERT_STREQ("CUSA05786", parts.title_id);
  TEST_ASSERT_STREQ("01.04", parts.version);
  TEST_ASSERT_STREQ("", parts.suffix);
  TEST_ASSERT_EQ_INT(0, parts.extension_rank);

  TEST_ASSERT_EQ_INT(0, onion_cheat_parse_filename(
                            "PPSA17168_01.004.000_97905f51.json", &parts));
  TEST_ASSERT_STREQ("PPSA17168", parts.title_id);
  TEST_ASSERT_STREQ("01.004.000", parts.version);
  TEST_ASSERT_STREQ("97905f51", parts.suffix);
  TEST_ASSERT_EQ_INT(0, parts.extension_rank);

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_parse_filename("CUSA05786_01.04_eboot.bin.json", &parts));
  TEST_ASSERT_STREQ("eboot.bin", parts.suffix);

  TEST_ASSERT_EQ_INT(
      0, onion_cheat_parse_filename("cusa12345-01.00.shn", &parts));
  TEST_ASSERT_STREQ("CUSA12345", parts.title_id);
  TEST_ASSERT_STREQ("01.00", parts.version);
  TEST_ASSERT_STREQ("", parts.suffix);
  TEST_ASSERT_EQ_INT(1, parts.extension_rank);

  TEST_ASSERT_EQ_INT(-1, onion_cheat_parse_filename("readme.txt", &parts));
  TEST_ASSERT_EQ_INT(-1, onion_cheat_parse_filename(NULL, &parts));
  TEST_ASSERT_EQ_INT(-1, onion_cheat_parse_filename("CUSA05786_01.04.json",
                                                    NULL));
  return 0;
}

static int test_legacy_eboot_alias(void) {
  TEST_ASSERT_EQ_INT(1, onion_cheat_is_legacy_eboot_alias("97905f51"));
  TEST_ASSERT_EQ_INT(1, onion_cheat_is_legacy_eboot_alias("eboot.bin"));
  TEST_ASSERT_EQ_INT(1, onion_cheat_is_legacy_eboot_alias("Aigars_Uze"));
  TEST_ASSERT_EQ_INT(0, onion_cheat_is_legacy_eboot_alias("worker.bin"));
  TEST_ASSERT_EQ_INT(0, onion_cheat_is_legacy_eboot_alias("tllr-boot.bin"));
  TEST_ASSERT_EQ_INT(0, onion_cheat_is_legacy_eboot_alias(""));
  TEST_ASSERT_EQ_INT(0, onion_cheat_is_legacy_eboot_alias(NULL));
  return 0;
}

static int test_normalize_version(void) {
  char out[32];

  onion_cheat_normalize_version("01.004.000", out, sizeof(out));
  TEST_ASSERT_STREQ("01.004.000", out);

  onion_cheat_normalize_version("1.0 (beta)", out, sizeof(out));
  TEST_ASSERT_STREQ("1.0__beta_", out);

  onion_cheat_normalize_version("a/b\\c", out, sizeof(out));
  TEST_ASSERT_STREQ("a_b_c", out);

  onion_cheat_normalize_version(NULL, out, sizeof(out));
  TEST_ASSERT_STREQ("", out);

  onion_cheat_normalize_version("x", out, 1); /* only room for NUL */
  TEST_ASSERT_STREQ("", out);

  onion_cheat_normalize_version("ab", out, 2); /* one char + NUL */
  TEST_ASSERT_STREQ("a", out);

  onion_cheat_normalize_filename_token("eboot/bin", out, sizeof(out));
  TEST_ASSERT_STREQ("eboot_bin", out);
  return 0;
}

int test_cheat_flatten_suite(void) {
  int failures = 0;
  failures += onion_test_run("flatten.match_ext_known", test_match_ext_known);
  failures += onion_test_run("flatten.match_ext_reject", test_match_ext_reject);
  failures += onion_test_run("flatten.flat_simple", test_flat_simple);
  failures += onion_test_run("flatten.flat_strips_process",
                             test_flat_strips_process_suffix);
  failures +=
      onion_test_run("flatten.flat_shnext_tid", test_flat_shnext_with_tid_prefix);
  failures +=
      onion_test_run("flatten.flat_uppercase", test_flat_uppercase_title);
  failures += onion_test_run("flatten.flat_dash", test_flat_dash_separator);
  failures += onion_test_run("flatten.flat_reject", test_flat_reject);
  failures += onion_test_run("flatten.parse_filename", test_parse_filename_parts);
  failures +=
      onion_test_run("flatten.legacy_eboot_alias", test_legacy_eboot_alias);
  failures +=
      onion_test_run("flatten.normalize_version", test_normalize_version);
  return failures;
}
