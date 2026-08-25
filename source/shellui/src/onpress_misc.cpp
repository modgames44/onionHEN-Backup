/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress misc (kstuff, account, credits) */
#include "onpress.hpp"
#include "account_activator.h"
#include <unistd.h>

static OnPressResult id_kstuff_autoload(OnPressContext &ctx) {
  const bool enabled = atol(ctx.value.c_str()) != 0;
  if (enabled == g_settings.kstuff_autoload)
    return OnPressResult::EarlyReturn;

  g_settings.kstuff_autoload = enabled;
  if (enabled) {
    unlink("/user/data/OnionHEN/no_kstuff");
    unlink("/data/OnionHEN/no_kstuff");
    notify("notify.kstuff.next_boot_on");
  } else {
    touch_file("/user/data/OnionHEN/no_kstuff");
    touch_file("/data/OnionHEN/no_kstuff");
    notify("notify.kstuff.next_boot_off");
  }
  return OnPressResult::Handled;
}

static OnPressResult id_delete_kstuff(OnPressContext &ctx) {
  (void)ctx;
  unlink("/user/data/OnionHEN/kstuff.elf");
  notify("notify.kstuff.deleted");
  return OnPressResult::Handled;
}

static OnPressResult id_activate_account(OnPressContext &ctx) {
  ctx.dirty = false;
  Activator activator(true);
  if (!activator.Valid()) {
    notify("notify.account.invalid");
    return OnPressResult::Consumed;
  }
  if (!activator.IsNotActivated()) {
    notify("notify.account.already");
    return OnPressResult::Consumed;
  }
  if (!activator.Activate()) {
    notify("notify.account.failed");
    return OnPressResult::Consumed;
  }
  notify("notify.account.activated");
  return OnPressResult::Consumed;
}

static OnPressResult id_lm_test(OnPressContext &ctx) {
  (void)ctx;
  LOG_DEBUG("LM's Test Button Pressed");
  return OnPressResult::Handled;
}

static OnPressResult id_onionhen_credits(OnPressContext &ctx) {
  (void)ctx;
  return OnPressResult::EarlyReturn;
}

static OnPressResult id_presentation_card(OnPressContext &ctx) {
  // Author and donator cards in the dynamic Toolbox XML are display-only.
  ctx.dirty = false;
  return OnPressResult::Consumed;
}

static const OnPressExactEntry kRootExact[] = {
    {"id_kstuff_autoload", id_kstuff_autoload},
    {"id_delete_kstuff", id_delete_kstuff},
    {"id_lm_test", id_lm_test},
    {"id_onionhen_credits", id_onionhen_credits},
    {"id_author_0xp0co", id_presentation_card},
    {"id_donator_aglx", id_presentation_card},
    {"id_donator_ljf", id_presentation_card},
    {"id_donator_szx", id_presentation_card},
};

const OnPressExactEntry *onpress_misc_root_exact(size_t *count) {
  *count = sizeof(kRootExact) / sizeof(kRootExact[0]);
  return kRootExact;
}

static const OnPressExactEntry kAccountExact[] = {
    {"id_activate_account", id_activate_account},
};

const OnPressExactEntry *onpress_account_exact(size_t *count) {
  *count = sizeof(kAccountExact) / sizeof(kAccountExact[0]);
  return kAccountExact;
}
