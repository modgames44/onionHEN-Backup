/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * PS5 Debug Settings XML fluent builder implementation.
 */

#include "ps5_settings_ui.hpp"

#include <sstream>

namespace ps5ui {
namespace {

const char* style_attr(Style style) {
  switch (style) {
  case Style::Center:
    return "center";
  case Style::Left:
    return "left";
  case Style::None:
  default:
    return nullptr;
  }
}

const char* kind_tag(Node::Kind kind) {
  switch (kind) {
  case Node::Kind::SettingList:
    return "setting_list";
  case Node::Kind::Toggle:
    return "toggle_switch";
  case Node::Kind::Button:
    return "button";
  case Node::Kind::Label:
    return "label";
  case Node::Kind::Link:
    return "link";
  case Node::Kind::List:
    return "list";
  case Node::Kind::ListItem:
    return "list_item";
  case Node::Kind::TextField:
    return "text_field";
  }
  return "unknown";
}

bool is_container(Node::Kind kind) {
  return kind == Node::Kind::SettingList || kind == Node::Kind::List;
}

void write_attr(std::ostringstream& out, const char* key, std::string_view value,
                bool path_escape) {
  out << ' ' << key << "=\""
      << (path_escape ? escape(value) : escape_xml(value)) << '"';
}

void write_optional(std::ostringstream& out, const char* key,
                    const std::optional<std::string>& value, bool path_escape) {
  if (value)
    write_attr(out, key, *value, path_escape);
}

void write_open_tag(std::ostringstream& out, const Node& node, bool self_close) {
  out << '<' << kind_tag(node.kind);
  /* Display text: single '/'. Icon filesystem paths: / → // for ShellUI. */
  write_attr(out, "id", node.attrs.id, /*path_escape=*/false);
  write_attr(out, "title", node.attrs.title, /*path_escape=*/false);
  write_optional(out, "second_title", node.attrs.second_title, false);
  write_optional(out, "description", node.attrs.description, false);
  write_optional(out, "icon", node.attrs.icon, /*path_escape=*/true);
  /* Relative plugin resource paths and reg keys keep single '/'. */
  write_optional(out, "file", node.attrs.file, false);
  write_optional(out, "key", node.attrs.key, false);
  write_optional(out, "keyboard_type", node.attrs.keyboard_type, false);
  write_optional(out, "min_length", node.attrs.min_length, false);
  write_optional(out, "max_length", node.attrs.max_length, false);
  write_optional(out, "value", node.attrs.value, false);
  write_optional(out, "confirm", node.attrs.confirm, false);
  write_optional(out, "confirm_phrase", node.attrs.confirm_phrase, false);
  write_optional(out, "initial_focus_to", node.attrs.initial_focus_to, false);
  if (const char* s = style_attr(node.attrs.style))
    write_attr(out, "style", s, false);
  out << (self_close ? "/>\n" : ">\n");
}

void serialize_node(std::ostringstream& out, const Node& node) {
  if (is_container(node.kind)) {
    write_open_tag(out, node, /*self_close=*/false);
    for (const Node& child : node.children)
      serialize_node(out, child);
    out << "</" << kind_tag(node.kind) << ">\n";
  } else {
    write_open_tag(out, node, /*self_close=*/true);
  }
}

} // namespace

std::string escape_xml(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

std::string escape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    case '/':
      out += "//";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

ListBuilder& ListBuilder::item(std::string id, std::string title,
                              std::string value,
                              std::optional<std::string> icon) {
  Node n;
  n.kind = Node::Kind::ListItem;
  n.attrs.id = std::move(id);
  n.attrs.title = std::move(title);
  n.attrs.value = std::move(value);
  n.attrs.icon = std::move(icon);
  list_node_.children.push_back(std::move(n));
  return *this;
}

Page::Page(std::string root_list_id, std::string root_title, std::string plugin)
    : plugin_(std::move(plugin)) {
  root_.kind = Node::Kind::SettingList;
  root_.attrs.id = std::move(root_list_id);
  root_.attrs.title = std::move(root_title);
  bind(&root_);
}

Page& Page::root_style(Style style) {
  root_.attrs.style = style;
  return *this;
}

Page& Page::root_focus(std::string id) {
  root_.attrs.initial_focus_to = std::move(id);
  return *this;
}

Page& Page::root_icon(std::string icon) {
  root_.attrs.icon = std::move(icon);
  return *this;
}

std::string Page::build() const {
  std::ostringstream out;
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
      << "<system_settings version=\"1.0\" plugin=\"" << escape_xml(plugin_)
      << "\">\n";
  serialize_node(out, root_);
  out << "</system_settings>\n";
  return out.str();
}

} // namespace ps5ui
