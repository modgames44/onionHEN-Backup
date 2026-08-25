/* Copyright (C) 2025 OnionHEN / LightningMods — P0 split. */


#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "external_symbols.hpp"
#include "detour.h"
#include "appinst_types.hpp"
#include <onion/platform.h>
#include <cstring>
#include <string>
#include <pthread.h>

extern bool is_6xx, is_3xx;
extern bool app_launched;

void ReloadApp(MonoString *str){
     std::string tid = mono_string_to_utf8(str);
     LOG_DEBUG("Reloading %s scenes", tid.c_str());
     notify("notify.app.reload_scenes", tid.c_str());
     Orig_ReloadApp(str);
}

#define PLAYGOSCENARIOID_SIZE 3
#define CONTENTID_SIZE 0x30
//int _AppInstUtilInstallByPackage(string uri, string ex_uri, string playgo_scenario_id, string content_id, string content_name, string icon_url, uint slot, bool is_playgo_enabled, ref AppInstUtilWrapper.SceAppInstallPkgInfo pkg_info, string[] languages, string[] playgo_scenario_ids, string[] content_ids);

int (*Orig_AppInstUtilInstallByPackage)(MonoString* uri, MonoString* ex_uri, MonoString* playgo_scenario_id, MonoString* content_id, MonoString* content_name, MonoString* icon_url, uint32_t slot, bool is_playgo_enabled, MonoObject* pkg_info, MonoArray* languages, MonoArray* playgo_scenario_ids, MonoArray* content_ids) = nullptr;

int AppInstUtilInstallByPackage_Hook(MonoString* uri, MonoString* ex_uri, MonoString* playgo_scenario_id, MonoString* content_id, MonoString* content_name, MonoString* icon_url, uint32_t slot, bool is_playgo_enabled, MonoObject* pkg_info, MonoArray* languages, MonoArray* playgo_scenario_ids, MonoArray* content_ids) {
        std::string s_uri = mono_string_to_utf8(uri);
    std::string s_ex_uri = mono_string_to_utf8(ex_uri);
    std::string s_playgo_scenario_id = mono_string_to_utf8(playgo_scenario_id);
    std::string s_content_id = mono_string_to_utf8(content_id);
    std::string s_content_name = mono_string_to_utf8(content_name);
    std::string s_icon_url = mono_string_to_utf8(icon_url);
    LOG_DEBUG("AppInstUtilInstallByPackage_Hook called with:\n uri: %s\n ex_uri: %s\n playgo_scenario_id: %s\n content_id: %s\n content_name: %s\n icon_url: %s\n slot: %u\n is_playgo_enabled: %d", 
        s_uri.c_str(), s_ex_uri.c_str(), s_playgo_scenario_id.c_str(), s_content_id.c_str(), s_content_name.c_str(), s_icon_url.c_str(), slot, is_playgo_enabled);
    notify("notify.pkg.installing", s_uri.c_str());
    int ret = Orig_AppInstUtilInstallByPackage(uri, ex_uri, playgo_scenario_id, content_id, content_name, icon_url, slot, is_playgo_enabled, pkg_info, languages, playgo_scenario_ids, content_ids);
    LOG_DEBUG("AppInstUtilInstallByPackage_Hook returned: %d", ret);
    notify("notify.pkg.finished", ret);
	return ret;
}

int (*sceAppInstUtilInstallByPackage_orig)(MetaInfo* arg1, SceAppInstallPkgInfo* pkg_info, PlayGoInfo* arg2) = nullptr;
void hex_dump(const char* label, const void* data, size_t size) {
    const unsigned char* bytes = (const unsigned char*)data;
    LOG_DEBUG("=== %s (size: %zu bytes) ===", label, size);

    for (size_t i = 0; i < size; i += 16) {
        char line[128];
        int offset = 0;

        // Print offset
        offset += snprintf(line + offset, sizeof(line) - offset,
            "%04zx: ", i);

        // Print hex values
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                offset += snprintf(line + offset, sizeof(line) - offset,
                    "%02x ", bytes[i + j]);
            }
            else {
                offset += snprintf(line + offset, sizeof(line) - offset,
                    "   ");
            }
        }

        // Print ASCII representation
        offset += snprintf(line + offset, sizeof(line) - offset, " |");
        for (size_t j = 0; j < 16 && i + j < size; j++) {
            unsigned char c = bytes[i + j];
            offset += snprintf(line + offset, sizeof(line) - offset,
                "%c", (c >= 32 && c <= 126) ? c : '.');
        }
        offset += snprintf(line + offset, sizeof(line) - offset, "|");

        LOG_DEBUG("%s", line);
    }
}
extern "C" int sceAppInstUtilInitialize();

int sceAppInstUtilInstallByPackage_hook(MetaInfo* arg1,
    SceAppInstallPkgInfo* pkg_info,
    PlayGoInfo* arg2) {

    LOG_DEBUG("========== sceAppInstUtilInstallByPackage_hook ==========");

    sceAppInstUtilInitialize();

    // Print MetaInfo
    if (arg1) {
        LOG_DEBUG("--- MetaInfo ---");
        LOG_DEBUG("uri: %s", arg1->uri ? arg1->uri : "(null)");
        LOG_DEBUG("ex_uri: %s", arg1->ex_uri ? arg1->ex_uri : "(null)");
        LOG_DEBUG("playgo_scenario_id: %s",
            arg1->playgo_scenario_id ? arg1->playgo_scenario_id : "(null)");
        LOG_DEBUG("content_id: %s",
            arg1->content_id ? arg1->content_id : "(null)");
        LOG_DEBUG("content_name: %s",
            arg1->content_name ? arg1->content_name : "(null)");
        LOG_DEBUG("icon_url: %s",
            arg1->icon_url ? arg1->icon_url : "(null)");
    }
    else {
        LOG_DEBUG("MetaInfo: (null)");
    }

    // Print SceAppInstallPkgInfo
    if (pkg_info) {
        LOG_DEBUG("--- SceAppInstallPkgInfo ---");
        LOG_DEBUG("content_id: %.*s", CONTENTID_SIZE, pkg_info->content_id);
        LOG_DEBUG("content_type: %d", pkg_info->content_type);
        LOG_DEBUG("content_platform: %d", pkg_info->content_platform);
    }
    else {
        LOG_DEBUG("SceAppInstallPkgInfo: (null)");
    }

    // Print PlayGoInfo
    if (arg2) {
        LOG_DEBUG("--- PlayGoInfo ---");

        // Print languages
        LOG_DEBUG("Languages:");
        for (int i = 0; i < NUM_LANGUAGES; i++) {
            if (arg2->languages[i][0] != '\0') {
                LOG_DEBUG("  [%d]: %.*s", i, LANGUAGE_SIZE,
                    arg2->languages[i]);
            }
        }

        // Print playgo_scenario_ids
        LOG_DEBUG("PlayGo Scenario IDs:");
        for (int i = 0; i < NUM_IDS; i++) {
            if (arg2->playgo_scenario_ids[i][0] != '\0') {
                LOG_DEBUG("  [%d]: %.*s", i, PLAYGOSCENARIOID_SIZE,
                    arg2->playgo_scenario_ids[i]);
            }
        }

        // Print content_ids
        LOG_DEBUG("Content IDs:");
        for (int i = 0; i < NUM_IDS; i++) {
            if (arg2->content_ids[i][0] != '\0') {
                LOG_DEBUG("  [%d]: %.*s", i, CONTENTID_SIZE,
                    arg2->content_ids[i]);
            }
        }

        // Hex dump unknown portion
        hex_dump("PlayGoInfo::unknown", arg2->unknown,
            sizeof(arg2->unknown));
    }
    else {
        LOG_DEBUG("PlayGoInfo: (null)");
    }

    LOG_DEBUG("========== Calling Original Function ==========");
    notify("notify.pkg.installing",
        arg1 ? (arg1->uri ? arg1->uri : "(null)") : "(null)");

    int ret = sceAppInstUtilInstallByPackage_orig(arg1, pkg_info, arg2);

    LOG_DEBUG("sceAppInstUtilInstallByPackage_hook returned: %d", ret);
    notify("notify.pkg.finished", ret);

    return ret;
}
// is_6xx/is_3xx defined in prx.cpp

void* dialogue_thread(void* arg) {
    while (true) {
        sleep(1);
        if(if_exists("/user/data/test.flag")){
        
           unlink("/user/data/test.flag");
       }
   }
    return nullptr;
}
