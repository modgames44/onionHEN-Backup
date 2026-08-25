#pragma once
// Shared AppInstUtil types for prx main + prx_install hooks.
#define LANGUAGE_SIZE 8
#define PLAYGOSCENARIOID_SIZE 3
#define CONTENTID_SIZE 0x30
typedef char playgo_scenario_id_t[PLAYGOSCENARIOID_SIZE];
typedef char language_t[LANGUAGE_SIZE];
typedef char content_id_t[CONTENTID_SIZE];
typedef struct {
    content_id_t content_id;
    int content_type;
    int content_platform;
} SceAppInstallPkgInfo;
typedef struct {
    const char* uri;
    const char* ex_uri;
    const char* playgo_scenario_id;
    const char* content_id;
    const char* content_name;
    const char* icon_url;
} MetaInfo;
#define NUM_LANGUAGES 30
#define NUM_IDS 64
typedef struct {
    language_t languages[NUM_LANGUAGES];
    playgo_scenario_id_t playgo_scenario_ids[NUM_IDS];
    content_id_t content_ids[NUM_IDS];
    long unknown[810];
} PlayGoInfo;
