/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Legacy Settings passes the actual UI3 ListPanelItem to
 * UserCustomElementUI.Reset. Sony later stores that same Widget in
 * ElementCreatedEventArgs for OnPostCreate, where stock pages append custom
 * panels. Hooking Reset gives the dynamic page the same parent Widget without
 * replacing a global PostCreate dispatcher.
 */

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "progress_dialog.hpp"
#include "shellui_state.hpp"

namespace {

void call_original(MonoObject *instance, MonoObject *item) {
  if (oUserCustomElementReset)
    oUserCustomElementReset(instance, item);
}

MonoObject *get_setting_element(MonoObject *instance) {
  if (!instance || !mono_class_from_name ||
      !mono_class_get_property_from_name || !mono_property_get_get_method ||
      !mono_runtime_invoke) {
    return nullptr;
  }

  MonoImage *legacy_image = getDLLimage(legacy_dec.c_str());
  MonoClass *base_class =
      legacy_image ? mono_class_from_name(legacy_image, UI3_dec.c_str(),
                                          "ElementUIBase")
                   : nullptr;
  MonoProperty *property = base_class
                               ? mono_class_get_property_from_name(base_class,
                                                                   "Element")
                               : nullptr;
  MonoMethod *getter = property ? mono_property_get_get_method(property) : nullptr;
  if (!getter)
    return nullptr;

  MonoObject *exc = nullptr;
  MonoObject *element = mono_runtime_invoke(getter, instance, nullptr, &exc);
  return exc ? nullptr : element;
}

} // namespace

void UserCustomElementReset_Hook(MonoObject *instance, MonoObject *item) {
  call_original(instance, item);

  if (!shellui_hooks_are_ready() || !instance || !item ||
      g_ui.active_page != toolbox::Page::CheatProgress) {
    return;
  }

  MonoObject *element = get_setting_element(instance);
  if (!element) {
    LOG_ERROR("cheat_progress_ui3: UserCustomElementUI.Element unavailable");
    return;
  }
  const std::string id = GetPropertyValue(element, "Id");
  LOG_DEBUG("cheat_progress_ui3: UserCustomElementUI.Reset id=%s", id.c_str());
  cheat_progress_attach_panel(id, item);
}
