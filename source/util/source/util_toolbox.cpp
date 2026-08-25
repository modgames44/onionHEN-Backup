/* Copyright (C) 2025 OnionHEN / LightningMods — P0 split. */


#include <onion/platform.h>
#include <onion/ready.h>
#include <onion/ipc_client.hpp>
#include <onion/settings.hpp>
#include <onion/toolbox_timing.h>
#include <msg.hpp>
#include "common_utils.h"
#include <atomic>
#include <unistd.h>
#include <cstring>

extern std::atomic_bool no_network_rest_mode_action;
extern std::atomic_bool no_network_patched;
extern std::atomic_bool real_rest_mode_detected;

extern "C" {
int sceUserServiceGetLoginUserIdList(void *list);
int sceUserServiceGetUserName(const int userId, char *userName, const size_t size);
}
struct UserServiceLoginUserIdList { int user_id[4]; };

bool enable_toolbox() {
    // Single client path into crit daemon (replaces hand-rolled Unix socket).
    // Wait briefly for crit socket / ready — bootstrap order is util then daemon.
    for (int wait = 0; wait <= 20; ++wait) {
      if (onion_ready_is_set(ONION_READY_DAEMON) || if_exists(CRIT_IPC_SOC)) {
        break;
      }
      if (wait == 20) {
        onion_notify(true, "notify.toolbox.load_failed");
        return false;
      }
      sleep(1);
    }
    return IPC_Client::getInstance(/*util=*/false).EnableToolbox();
}


bool isUserLoggedIn() {
    bool isLoggedIn = false;
    UserServiceLoginUserIdList userIdList;
    (void)memset(&userIdList, 0, sizeof(UserServiceLoginUserIdList));
    
    if (sceUserServiceGetLoginUserIdList(&userIdList) < 0) {
        return false;
    }

    for (int i = 0; i < 4; i++) {
        char username[500] = {0};
        int userid = userIdList.user_id[i];
        if (userid != -1) {
            int ret = sceUserServiceGetUserName(userid, &username[0], sizeof(username));
            LOG_INFO("sceUserServiceGetUserName returned %d", ret);
            if (ret == 0) {
                isLoggedIn = true;
                break;
            }
        }
    }
    
    sleep(5);
    return isLoggedIn;
}
/**
 * Re-request toolbox inject via crit daemon.
 * @param rest_resume  true only for real rest-mode recovery paths.
 *                     false for util restart / re-HEN reinject (no rest copy).
 */
void patch_checker(bool rest_resume) {
    if (!isUserLoggedIn()) {
        LOG_WARN("User is not logged in yet, skipping toolbox reinject...");
        return;
    }

    LoadSettings();
    const onion::Settings cfg = g_settings.snapshot();

    if (rest_resume &&
        onion_toolbox_should_apply_rest_delay(true, cfg.rest_mode_delay_seconds)) {
        LOG_INFO("rest resume delay %llu secs",
                     static_cast<unsigned long long>(cfg.rest_mode_delay_seconds));
        sleep(static_cast<unsigned int>(cfg.rest_mode_delay_seconds));
        onion_notify(true,
                     "notify.rest.reactivating");
    } else {
        LOG_INFO("toolbox reinject (not rest resume)");
    }

    if (!enable_toolbox()) {
        onion_notify(true, "notify.toolbox.inject_failed");
    }

    if (rest_resume) {
        no_network_rest_mode_action = false;
        no_network_patched = true;
        real_rest_mode_detected = false;
    }
}
