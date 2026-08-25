/* Host unit tests for toolbox::resolve_resource state machine (no PS5/Mono). */
#include "test_harness.h"

#include "toolbox_route.hpp"

#include <cstring>
#include <string>

using namespace toolbox;

static ResourceNames test_names() {
  return ResourceNames{
      .payloads_xml = "payloads.xml",
      .debug_settings_xml = "debug_settings.xml",
      .cheats_xml = "cheats.xml",
  };
}

static RouteInput make_in(std::string_view resource, bool sc = false,
                          bool sc_not_open = false) {
  return RouteInput{
      .resource = resource,
      .names = test_names(),
      .cheats_shortcut = sc,
      .cheats_shortcut_not_open = sc_not_open,
  };
}

static int test_unknown_passthrough(void) {
  RouteResult r = resolve_resource(make_in("something.else.xml"));
  TEST_ASSERT_TRUE(r.page == Page::None);
  TEST_ASSERT_TRUE(!r.flags.is_payloads);
  TEST_ASSERT_TRUE(!r.flags.is_cheats);
  return 0;
}

static int test_payloads_page(void) {
  RouteResult r = resolve_resource(make_in("payloads.xml"));
  TEST_ASSERT_TRUE(r.page == Page::Payloads);
  TEST_ASSERT_TRUE(r.flags.is_payloads);
  TEST_ASSERT_TRUE(!r.flags.is_cheats);
  return 0;
}

static int test_debug_settings_page(void) {
  RouteResult r = resolve_resource(make_in("debug_settings.xml"));
  TEST_ASSERT_TRUE(r.page == Page::DebugSettings);
  TEST_ASSERT_TRUE(r.flags.is_debug_settings);
  return 0;
}

static int test_cheats_page(void) {
  RouteResult r = resolve_resource(make_in("cheats.xml"));
  TEST_ASSERT_TRUE(r.page == Page::Cheats);
  TEST_ASSERT_TRUE(r.flags.is_cheats);
  TEST_ASSERT_TRUE(r.clear_cheat_shortcuts_after);
  return 0;
}

static int test_auto_payloads_and_plapps(void) {
  RouteResult a = resolve_resource(make_in(kAutoPayloadsXml));
  TEST_ASSERT_TRUE(a.page == Page::AutoPayloads);
  TEST_ASSERT_TRUE(a.flags.is_auto_payload);

  RouteResult p = resolve_resource(make_in(kPlappsXml));
  TEST_ASSERT_TRUE(p.page == Page::Plapps);
  TEST_ASSERT_TRUE(p.flags.is_plapps);
  return 0;
}

static int test_account_page(void) {
  RouteResult r = resolve_resource(make_in(kAccountXml));
  TEST_ASSERT_TRUE(r.page == Page::Account);
  TEST_ASSERT_TRUE(r.flags.is_account);
  return 0;
}

static int test_superuser_pass_through(void) {
  RouteResult r = resolve_resource(make_in(kSuperuserXml));
  TEST_ASSERT_TRUE(r.page == Page::SuperuserPass);
  TEST_ASSERT_TRUE(r.flags.is_su_menu);
  return 0;
}

static int test_og_debug_redirect(void) {
  RouteResult r = resolve_resource(make_in(kOgDebugXml));
  TEST_ASSERT_TRUE(r.page == Page::RedirectOgDebug);
  return 0;
}

static int test_shortcut_force_cheats(void) {
  RouteResult r = resolve_resource(make_in("debug_settings.xml", true, false));
  TEST_ASSERT_TRUE(r.page == Page::Cheats);
  TEST_ASSERT_TRUE(r.shortcut_forced_cheats);
  TEST_ASSERT_TRUE(!r.flags.is_debug_settings);
  return 0;
}

static int test_shortcut_not_open(void) {
  RouteResult r = resolve_resource(make_in("debug_settings.xml", false, true));
  TEST_ASSERT_TRUE(r.page == Page::Cheats);
  TEST_ASSERT_TRUE(r.shortcut_forced_cheats);
  return 0;
}

static int test_matrix(void) {
  /* quick matrix: each named resource maps distinctly */
  TEST_ASSERT_TRUE(resolve_resource(make_in("payloads.xml")).page ==
                   Page::Payloads);
  TEST_ASSERT_TRUE(resolve_resource(make_in("cheats.xml")).page == Page::Cheats);
  TEST_ASSERT_TRUE(resolve_resource(make_in("debug_settings.xml")).page ==
                   Page::DebugSettings);
  return 0;
}

static int test_session_flags_clear(void) {
  RouteFlags f{};
  f.is_payloads = true;
  f.is_cheats = true;
  TEST_ASSERT_TRUE(f.is_payloads && f.is_cheats);
  f = RouteFlags{};
  TEST_ASSERT_TRUE(!f.is_payloads && !f.is_cheats);
  return 0;
}

static int test_cheatmap_tid_reset(void) {
  std::string tid = "A";
  int map[kCheatMapSize]{};
  map[1] = 1;
  TEST_ASSERT_TRUE(reset_cheat_map_if_tid_changed(tid, map, kCheatMapSize, "B"));
  TEST_ASSERT_STREQ("B", tid.c_str());
  TEST_ASSERT_EQ_INT(0, map[1]);
  TEST_ASSERT_TRUE(!reset_cheat_map_if_tid_changed(tid, map, kCheatMapSize, "B"));
  return 0;
}

static int test_cheatmap_bounds(void) {
  int map[kCheatMapSize]{};
  set_cheat_enabled(map, kCheatMapSize, 0, true);
  set_cheat_enabled(map, kCheatMapSize, -1, true);
  set_cheat_enabled(map, kCheatMapSize, (int)kCheatMapSize, true);
  TEST_ASSERT_TRUE(get_cheat_enabled(map, kCheatMapSize, 0));
  TEST_ASSERT_TRUE(!get_cheat_enabled(map, kCheatMapSize, -1));
  TEST_ASSERT_TRUE(!get_cheat_enabled(map, kCheatMapSize, (int)kCheatMapSize));
  return 0;
}

extern "C" int test_toolbox_route_suite(void) {
  int fails = 0;
  fails += onion_test_run("route.unknown", test_unknown_passthrough);
  fails += onion_test_run("route.payloads", test_payloads_page);
  fails += onion_test_run("route.debug", test_debug_settings_page);
  fails += onion_test_run("route.cheats", test_cheats_page);
  fails += onion_test_run("route.auto_plapps", test_auto_payloads_and_plapps);
  fails += onion_test_run("route.account", test_account_page);
  fails += onion_test_run("route.superuser", test_superuser_pass_through);
  fails += onion_test_run("route.og_debug", test_og_debug_redirect);
  fails += onion_test_run("route.shortcut_force", test_shortcut_force_cheats);
  fails += onion_test_run("route.shortcut_not_open", test_shortcut_not_open);
  fails += onion_test_run("route.matrix", test_matrix);
  fails += onion_test_run("session.flags_clear", test_session_flags_clear);
  fails += onion_test_run("cheatmap.tid_reset", test_cheatmap_tid_reset);
  fails += onion_test_run("cheatmap.bounds", test_cheatmap_bounds);
  return fails;
}
