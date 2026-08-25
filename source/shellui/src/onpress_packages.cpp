/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress payload apps */
#include "onpress.hpp"

static OnPressResult prefix_id_pl_loader(OnPressContext &ctx) {
  if (ctx.id.rfind("id_onionhen_pl_loader_", 0) != 0) {
    return OnPressResult::NotMine;
  }
  if (g_ui.games_list.empty()) {
    return OnPressResult::EarlyReturn;
  }
  for (const auto &game : g_ui.games_list) {
    if (game.id == ctx.id) {
      break;
    }
  }
  return OnPressResult::Handled;
}

static const OnPressPrefixEntry kPrefix[] = {
    {"id_onionhen_pl_loader_", prefix_id_pl_loader},
};

const OnPressPrefixEntry *onpress_packages_prefix(size_t *count) {
  *count = sizeof(kPrefix) / sizeof(kPrefix[0]);
  return kPrefix;
}
