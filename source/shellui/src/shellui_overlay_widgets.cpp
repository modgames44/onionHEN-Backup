/* Copyright (C) 2025 OnionHEN / LightningMods — P0 split.
 *
 * Horizontal monitor bar:
 *   font_size=18, font_style=1 (Bold), font_weight=900
 *   per-metric label colors; values white
 *   bg_color=000000B4 (~70% black Panel)
 *   EnableThemedTextShadow, UI2.Panel Background*
 */

#include "hooked_funcs.hpp"
#include "external_symbols.hpp"
#include "ipc.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

extern MonoObject *Game;
extern MonoObject *rootWidget;
extern MonoObject *font;
extern OverlayLayout g_overlay_layout;
#include "shellui_state.hpp"
extern onion::Settings g_settings;

MonoObject *CreateLabel(const char *name, float x, float y, const char *text,
                        MonoObject *fontObj, int horzAlign, int vertAlign,
                        float r, float g, float b, float a);
void Widget_Append_Child(MonoObject *widget, MonoObject *child);
MonoObject *CreateUIFont(int size, int style, int weight);
MonoObject *CreateUIColor(float r, float g, float b, float a);

namespace {

constexpr const char *kBgPanelName = "id_onion_overlay_bg";

constexpr int kFontSize = 18;
constexpr int kFontStyle = 1;    /* 1 = Bold bit */
constexpr int kFontWeight = 900; /* CSS black */

/* Intra-segment X offsets from the segment origin. */
constexpr float kLbl = 0.0f;
constexpr float kVal0 = 46.0f;
constexpr float kVal1 = 102.0f;
constexpr float kIpVal = 34.0f;

/*
 * Per-group colors (RRGGBBAA → linear 0..1).
 * Labels use group color; all values use white.
 */
/* color_cpu  = 66FF66FF — green */
constexpr float kCpuR = 102.0f / 255.0f, kCpuG = 1.0f, kCpuB = 102.0f / 255.0f;
/* color_gpu  = B366FFFF — purple */
constexpr float kGpuR = 179.0f / 255.0f, kGpuG = 102.0f / 255.0f, kGpuB = 1.0f;
/* color_mem  = FFB34DFF — orange */
constexpr float kMemR = 1.0f, kMemG = 179.0f / 255.0f, kMemB = 77.0f / 255.0f;
/* color_net  = 80B3FFFF — blue */
constexpr float kNetR = 128.0f / 255.0f, kNetG = 179.0f / 255.0f, kNetB = 1.0f;
/* color_value = FFFFFFFF */
constexpr float kValR = 1.0f, kValG = 1.0f, kValB = 1.0f;
/* Separator pipe — soft white so it does not compete with values. */
constexpr float kSepR = 0.75f, kSepG = 0.75f, kSepB = 0.75f;
/* bg_color = 000000B4 → black @ ~70.6% */
constexpr float kBgR = 0.0f, kBgG = 0.0f, kBgB = 0.0f, kBgA = 180.0f / 255.0f;
constexpr float kA = 1.0f;

/* VerticalAlignment=0 with FitHeightToText=true for bar labels. */
constexpr int kAlignLeft = 0;
constexpr int kAlignTop = 0;
constexpr float kInitialLabelWidth = 500.0f;

void push_label(std::vector<WidgetConfig> &out, const char *id, float x, float y,
                const char *text, float r, float g, float b) {
  /* bold field → HorizontalAlignment (left). Vertical center set at create. */
  out.push_back({id, x, y, text, kAlignLeft, r, g, b, kA});
}

void append_sep(std::vector<WidgetConfig> &out, const char *id, float x,
                float y) {
  push_label(out, id, x, y, "|", kSepR, kSepG, kSepB);
}

void append_cpu(std::vector<WidgetConfig> &out) {
  const float x = g_overlay_layout.overlay_cpu_x;
  const float y = g_overlay_layout.overlay_cpu_y;
  push_label(out, "id_cpu_label", x + kLbl, y, "CPU", kCpuR, kCpuG, kCpuB);
  push_label(out, "id_cpu_temp_value", x + kVal0, y, "--C", kValR, kValG, kValB);
  push_label(out, "id_cpu_usage_value", x + kVal1, y, "--%", kValR, kValG, kValB);
  append_sep(out, "id_cpu_sep", x + kVal1 + 56.0f, y);
}

void append_gpu(std::vector<WidgetConfig> &out) {
  const float x = g_overlay_layout.overlay_gpu_x;
  const float y = g_overlay_layout.overlay_gpu_y;
  push_label(out, "id_gpu_label", x + kLbl, y, "GPU", kGpuR, kGpuG, kGpuB);
  push_label(out, "id_gpu_temp_value", x + kVal0, y, "--C", kValR, kValG, kValB);
  push_label(out, "id_gpu_usage_value", x + kVal1, y, "--%", kValR, kValG, kValB);
  append_sep(out, "id_gpu_sep", x + kVal1 + 56.0f, y);
}

void append_ram(std::vector<WidgetConfig> &out) {
  const float x = g_overlay_layout.overlay_ram_x;
  const float y = g_overlay_layout.overlay_ram_y;
  push_label(out, "id_ram_label", x + kLbl, y, "RAM", kMemR, kMemG, kMemB);
  push_label(out, "id_ram_value", x + kVal0, y, "---- MB", kValR, kValG, kValB);
  append_sep(out, "id_ram_sep", x + kVal0 + 80.0f, y);
}

void append_ip(std::vector<WidgetConfig> &out) {
  const float x = g_overlay_layout.overlay_ip_x;
  const float y = g_overlay_layout.overlay_ip_y;
  push_label(out, "id_ip_label", x + kLbl, y, "IP", kNetR, kNetG, kNetB);
  push_label(out, "id_ip_value", x + kIpVal, y, "---.---.---.---", kValR, kValG,
             kValB);
  append_sep(out, "id_ip_sep", x + kIpVal + 160.0f, y);
}

const std::vector<const char *> kAllOverlayNames = {
    kBgPanelName,
    "id_gpu_temp_value", "id_gpu_usage_value", "id_gpu_label", "id_gpu_sep",
    "id_cpu_label",      "id_cpu_temp_value",  "id_cpu_usage_value",
    "id_cpu_sep",
    "id_ram_label",      "id_ram_value",       "id_ram_sep",
    "id_ip_label",       "id_ip_value",        "id_ip_sep",
};

MonoObject *find_root_widget() {
  return Get_Property<MonoObject *>(pui_img, "Sce.PlayStation.PUI.UI2", "Scene",
                                    Game, "RootWidget");
}

void remove_named(MonoObject *root, const char *name) {
  if (!root || !name)
    return;
  MonoClass *widgetClass =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
  MonoObject *child =
      Invoke<MonoObject *>(pui_img, widgetClass, root, "FindWidgetByName",
                           mono_string_new(Root_Domain, name));
  if (child)
    Invoke<void>(pui_img, widgetClass, child, "RemoveFromParent");
}

void remove_metric_cell(MonoObject *root, const char *label_name) {
  char cell_name[96];
  std::snprintf(cell_name, sizeof(cell_name), "%s_cell", label_name);
  MonoClass *widgetClass =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
  MonoObject *cell =
      Invoke<MonoObject *>(pui_img, widgetClass, root, "FindWidgetByName",
                           mono_string_new(Root_Domain, cell_name));
  if (cell)
    Invoke<void>(pui_img, widgetClass, cell, "RemoveFromParent");
  else
    remove_named(root, label_name);
}

MonoObject *create_metric_cell(MonoObject *root, const char *label_name,
                               float x) {
  MonoClass *panelClass =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Panel");
  if (!panelClass)
    return nullptr;

  MonoObject *cell = New_Object(panelClass);
  if (!cell)
    return nullptr;
  mono_runtime_object_init(cell);

  char cell_name[96];
  std::snprintf(cell_name, sizeof(cell_name), "%s_cell", label_name);
  Set_Property(panelClass, cell, "Name",
               mono_string_new(Root_Domain, cell_name));
  Set_Property(panelClass, cell, "X", x);
  Set_Property(panelClass, cell, "Y", g_overlay_layout.bar_y);
  Set_Property(panelClass, cell, "Width", kInitialLabelWidth);
  Set_Property(panelClass, cell, "Height", g_overlay_layout.bar_h);
  Set_Property(panelClass, cell, "BackgroundVisibility", false);
  Widget_Append_Child(root, cell);
  return cell;
}

/** Prefer live RootWidget.Width; fall back to layout bar_w (1920). */
float resolve_panel_width(MonoObject *root) {
  if (!root)
    return g_overlay_layout.bar_w;
  MonoClass *widgetClass =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
  /* Width is on Widget base; Get_Property may return boxed float. */
  float w = Get_Property<float>(widgetClass, root, "Width");
  if (w > 1.0f)
    return w;
  return g_overlay_layout.bar_w > 1.0f ? g_overlay_layout.bar_w : 1920.0f;
}

/**
 * Edge-flush full-width background Panel
 *   X=0, Y=0 (or bottom), Width=RootWidget width,
 *   Height=font+6, bg_color=000000B4.
 */
void ensure_bg_panel(MonoObject *root) {
  if (!root)
    return;

  /* Hide strip when nothing is enabled (bar_h still set; content empty). */
  const bool any =
      g_settings.overlay_enabled && g_settings.overlay_background &&
      (g_settings.overlay_cpu || g_settings.all_cpu_usage ||
       g_settings.overlay_gpu || g_settings.overlay_ram || g_settings.overlay_ip);
  if (!any)
    return;

  MonoClass *widgetClass =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
  MonoObject *existing =
      Invoke<MonoObject *>(pui_img, widgetClass, root, "FindWidgetByName",
                           mono_string_new(Root_Domain, kBgPanelName));
  if (existing)
    return;

  MonoClass *panelClass =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Panel");
  if (!panelClass) {
    LOG_ERROR("overlay: UI2.Panel class missing — no bg bar");
    return;
  }

  MonoObject *panel = New_Object(panelClass);
  if (!panel) {
    LOG_ERROR("overlay: failed to alloc Panel");
    return;
  }
  mono_runtime_object_init(panel);

  const float panel_w = resolve_panel_width(root);

  Set_Property(panelClass, panel, "Name",
               mono_string_new(Root_Domain, kBgPanelName));
  /* X=0, Y=edge, Width=fullscreen root width. */
  Set_Property(panelClass, panel, "X", 0.0f);
  Set_Property(panelClass, panel, "Y", g_overlay_layout.bar_y);
  Set_Property(panelClass, panel, "Width", panel_w);
  Set_Property(panelClass, panel, "Height", g_overlay_layout.bar_h);

  MonoObject *bg = CreateUIColor(kBgR, kBgG, kBgB, kBgA);
  if (bg)
    Set_Property_Invoke(panelClass, panel, "BackgroundColor", bg);

  Set_Property(panelClass, panel, "BackgroundVisibility", true);
  Set_Property(panelClass, panel, "BackgroundOpacity", 1.0f);
  Set_Property(panelClass, panel, "BackgroundStyle", 1);

  Widget_Append_Child(root, panel);
}

} // namespace

void RemoveGameWidget(RemoveWidget widget) {
  auto removeWidgets = [](const std::vector<const char *> &widgetNames) {
    MonoObject *root = find_root_widget();
    for (const char *name : widgetNames) {
      if (std::strcmp(name, kBgPanelName) == 0)
        remove_named(root, name);
      else
        remove_metric_cell(root, name);
    }
  };

  switch (widget) {
  case REMOVE_GPU_OVERLAY:
    removeWidgets(
        {"id_gpu_temp_value", "id_gpu_usage_value", "id_gpu_label", "id_gpu_sep"});
    break;
  case REMOVE_CPU_OVERLAY:
    removeWidgets({"id_cpu_label", "id_cpu_temp_value", "id_cpu_usage_value",
                   "id_cpu_sep"});
    break;
  case REMOVE_RAM_OVERLAY:
    removeWidgets({"id_ram_label", "id_ram_value", "id_ram_sep"});
    break;
  case REMOVE_IP_OVERLAY:
    removeWidgets({"id_ip_label", "id_ip_value", "id_ip_sep"});
    break;
  case REMOVE_ALL_OVERLAYS:
    removeWidgets(kAllOverlayNames);
    break;
  }
}

void CreateGameWidget(CreateWidget widget) {
  if (!g_settings.overlay_enabled)
    return;

  MonoObject *bar_font = CreateUIFont(kFontSize, kFontStyle, kFontWeight);
  MonoObject *root = find_root_widget();
  if (!root)
    return;

  ensure_bg_panel(root);

  std::vector<WidgetConfig> configs;
  switch (widget) {
  case CREATE_GPU_OVERLAY:
    append_gpu(configs);
    break;
  case CREATE_CPU_OVERLAY:
    append_cpu(configs);
    break;
  case CREATE_RAM_OVERLAY:
    append_ram(configs);
    break;
  case CREATE_IP_OVERLAY:
    append_ip(configs);
    break;
  case CREATE_ALL_OVERLAYS:
    append_cpu(configs);
    append_gpu(configs);
    append_ram(configs);
    append_ip(configs);
    break;
  }

  MonoClass *labelClass =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");

  for (const auto &config : configs) {
    MonoObject *cell = create_metric_cell(root, config.id, config.x);
    MonoObject *label = CreateLabel(
        config.id, 0.0f, g_overlay_layout.label_margin_top, config.text,
        bar_font, config.bold, kAlignTop, config.r, config.g, config.b,
        config.a);
    if (label && labelClass) {
      Set_Property(labelClass, label, "PositionType", 1);
      Set_Property(labelClass, label, "MarginLeft", 0.0f);
      Set_Property(labelClass, label, "MarginTop",
                   g_overlay_layout.label_margin_top);
      Set_Property(labelClass, label, "Width", kInitialLabelWidth);
      Set_Property(labelClass, label, "HorizontalAlignment", kAlignLeft);
      Set_Property(labelClass, label, "VerticalAlignment", kAlignTop);
      Set_Property(labelClass, label, "FitWidthToText", false);
      Set_Property(labelClass, label, "FitHeightToText", true);
      Set_Property(labelClass, label, "NumberOfLines", 1);
    }
    Widget_Append_Child(cell ? cell : root, label);
  }
}
