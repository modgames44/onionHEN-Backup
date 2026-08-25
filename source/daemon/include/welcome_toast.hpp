#pragma once

#include <onion/debug_settings_route_policy.hpp>
#include <onion/notify_i18n.h>
#include <onion/version.h>

#include <string>
#include <string_view>

namespace onion::daemon {

inline constexpr const char kWelcomeToastToolboxUriToken[] =
    "__ONIONHEN_TOOLBOX_URI__";
inline constexpr const char kWelcomeToastMessageToken[] =
    "__ONIONHEN_WELCOME_MESSAGE__";
inline constexpr const char kWelcomeToastSubMessageToken[] =
    "__ONIONHEN_WELCOME_SUB_MESSAGE__";
inline constexpr const char kWelcomeToastActionToken[] =
    "__ONIONHEN_WELCOME_ACTION__";

inline constexpr const char kWelcomeToastJsonTemplate[] =
    "{\n"
    "  \"rawData\": {\n"
    "    \"viewTemplateType\": \"InteractiveToastTemplateB\",\n"
    "    \"channelType\": \"Downloads\",\n"
    "    \"useCaseId\": \"IDC\",\n"
    "    \"toastOverwriteType\": \"No\",\n"
    "    \"isImmediate\": true,\n"
    "    \"priority\": 100,\n"
    "    \"viewData\": {\n"
    "      \"icon\": {\n"
    "        \"type\": \"Url\",\n"
    "        \"parameters\": {\n"
    "          \"url\": \"/user/data/OnionHEN/onionhen.png\"\n"
    "        }\n"
    "      },\n"
    "      \"message\": {\n"
    "        \"body\": \"__ONIONHEN_WELCOME_MESSAGE__\"\n"
    "      },\n"
    "      \"subMessage\": {\n"
    "        \"body\": \"__ONIONHEN_WELCOME_SUB_MESSAGE__\"\n"
    "      },\n"
    "      \"actions\": [\n"
    "        {\n"
    "          \"actionName\": \"__ONIONHEN_WELCOME_ACTION__\",\n"
    "          \"actionType\": \"DeepLink\",\n"
    "          \"defaultFocus\": true,\n"
    "          \"parameters\": {\n"
    "            \"actionUrl\": \"__ONIONHEN_TOOLBOX_URI__\"\n"
    "          }\n"
    "        }\n"
    "      ]\n"
    "    },\n"
    "    \"platformViews\": {\n"
    "      \"previewDisabled\": {\n"
    "        \"viewData\": {\n"
    "          \"icon\": {\n"
    "            \"type\": \"Predefined\",\n"
    "            \"parameters\": {\n"
    "              \"icon\": \"download\"\n"
    "            }\n"
    "          },\n"
    "          \"message\": {\n"
    "            \"body\": \"__ONIONHEN_WELCOME_MESSAGE__\"\n"
    "          }\n"
    "        }\n"
    "      }\n"
    "    }\n"
    "  },\n"
    "  \"createdDateTime\": \"2025-12-14T03:14:51.473Z\",\n"
    "  \"localNotificationId\": \"588193127\"\n"
    "}";

inline void replace_all(std::string &text, std::string_view token,
                        std::string_view replacement) {
  size_t pos = 0;
  while ((pos = text.find(token, pos)) != std::string::npos) {
    text.replace(pos, token.size(), replacement.data(), replacement.size());
    pos += replacement.size();
  }
}

inline std::string make_welcome_toast_json(std::string_view toolbox_uri) {
  std::string json = kWelcomeToastJsonTemplate;
  const std::string message = std::string(ONIONHEN_VERSION) +
                              onion_notify_tr("notify.boot.made_by") + ONIONHEN_AUTHOR;
  replace_all(json, kWelcomeToastMessageToken, message);
  replace_all(json, kWelcomeToastSubMessageToken,
              onion_notify_tr("notify.boot.welcome"));
  replace_all(json, kWelcomeToastActionToken,
              onion_notify_tr("notify.boot.goto_toolbox"));
  const std::string_view replacement =
      toolbox_uri.empty()
          ? std::string_view(
                onion::debug_settings_route::kStandardRoute.simple_uri)
          : toolbox_uri;

  replace_all(json, kWelcomeToastToolboxUriToken, replacement);
  return json;
}

} // namespace onion::daemon
