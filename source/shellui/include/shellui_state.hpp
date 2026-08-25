/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Toolbox / settings UI runtime state. All ShellUI session state lives on g_ui.
 */
#pragma once

#include "shellui_types.hpp"
#include "toolbox_route.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

/** Settings page / resource-stream context for ShellUI hooks. */
struct ToolboxUiState {
  toolbox::Page active_page = toolbox::Page::None;

  bool cheats_shortcut_activated = false;
  bool cheats_shortcut_activated_not_open = false;

  std::string running_tid;
  bool is_game_open = true;
  bool is_current_game_open = true;
  std::string current_menu_tid;
  std::string current_cheat_tid;
  int cheat_enabled_map[toolbox::kCheatMapSize]{};

  std::vector<PayloadEntry> payloads_list;
  std::vector<PayloadEntry> auto_payloads_list;
  std::vector<Payloads_Apps> payloads_apps_list;
  std::vector<GameEntry> games_list;

  void set_active_page(toolbox::Page page) {
    active_page = page;
  }

  bool is_active_page(toolbox::Page page) const {
    return active_page == page;
  }

  void leave_page(toolbox::Page page) {
    if (active_page == page)
      active_page = toolbox::Page::None;
  }

  void clear_cheat_shortcuts() {
    cheats_shortcut_activated = false;
    cheats_shortcut_activated_not_open = false;
  }

  bool any_cheat_shortcut() const {
    return cheats_shortcut_activated || cheats_shortcut_activated_not_open;
  }

  bool reset_cheats_if_tid_changed(std::string_view new_tid) {
    return toolbox::reset_cheat_map_if_tid_changed(
        current_cheat_tid, cheat_enabled_map, toolbox::kCheatMapSize, new_tid);
  }

  void set_cheat_enabled(int cheat_id, bool enabled) {
    toolbox::set_cheat_enabled(cheat_enabled_map, toolbox::kCheatMapSize,
                               cheat_id, enabled);
  }

  bool get_cheat_enabled(int cheat_id) const {
    return toolbox::get_cheat_enabled(cheat_enabled_map, toolbox::kCheatMapSize,
                                      cheat_id);
  }
};

extern ToolboxUiState g_ui;
