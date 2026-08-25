/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress network domain */
#include "onpress.hpp"
#include <cstdlib>

static OnPressResult id_disp_titleids(OnPressContext &ctx) {
  bool &dis_tids = g_settings.display_tids;
  if (atol(ctx.value.c_str()) == dis_tids) {
    LOG_WARN("Display TIDs already %s", dis_tids ? "Enabled" : "Disabled");
    return OnPressResult::EarlyReturn;
  }
  dis_tids = !dis_tids;
  ReloadRNPSApp("NPXS40002");
  return OnPressResult::Handled;
}

static const OnPressExactEntry kExact[] = {
    {"id_disp_titleids", id_disp_titleids},
};

const OnPressExactEntry *onpress_network_exact(size_t *count) {
  *count = sizeof(kExact) / sizeof(kExact[0]);
  return kExact;
}
