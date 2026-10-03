#include <pebble.h>
#include "storage.h"

#define DRINKS_PERSIST_KEY 1
#define NUM_DRINKS_PERSIST_KEY 2
#define SETTINGS_PERSIST_KEY 3

#if !defined(PBL_PLATFORM_APLITE)
typedef struct { time_t timestamp; float volume_ml; float abv; } DrinkV0;
typedef struct { time_t timestamp; float volume_ml; float abv; DrinkShape shape; } DrinkV1;
typedef struct { float weight; float gender_constant; bool use_metric_volume; bool use_metric_weight; ThemeMode theme_mode; bool enable_portions; bool right_handed_mode; bool auto_exit; } AppSettingsV1;
typedef struct { float weight; float gender_constant; bool use_metric_volume; bool use_metric_weight; ThemeMode theme_mode; bool enable_portions; bool right_handed_mode; uint8_t idle_timeout_mins; float target_bac; bool auto_exit; } AppSettingsV2;
typedef struct { float weight; float gender_constant; bool use_metric_volume; bool use_metric_weight; ThemeMode theme_mode; bool enable_portions; bool right_handed_mode; uint8_t idle_timeout_mins; float target_bac; bool auto_exit; uint8_t region; } AppSettingsV3;
#endif

// FIXED: Will only be allocated ONCE to prevent heap fragmentation
static Drink *s_drinks = NULL;
static int s_num_drinks = 0;

static AppSettings s_settings = {
    .weight = 80.0f, .gender_constant = 0.68f, .use_metric_volume = true, .use_metric_weight = true,
    .theme_mode = THEME_MODE_LIGHT, .enable_portions = true, .right_handed_mode = false,
    .idle_timeout_mins = 0, .target_bac = 0.00f, .auto_exit = false, .region = REGION_NA,
    .last_custom_volume_ml = 0.0f, .last_custom_abv = 5.0f
};

static void sanitize_settings(void) {
    if (s_settings.idle_timeout_mins > 15) s_settings.idle_timeout_mins = 0;
    if (s_settings.region > REGION_AU) s_settings.region = REGION_NA;
    if (!(s_settings.target_bac >= -1.0f && s_settings.target_bac <= 0.40f)) s_settings.target_bac = 0.00f;
    if (!(s_settings.weight >= 20.0f && s_settings.weight <= 600.0f)) s_settings.weight = 80.0f;
    if (!(s_settings.gender_constant >= 0.5f && s_settings.gender_constant <= 1.0f)) s_settings.gender_constant = 0.68f;
    if (s_settings.weight < 1.0f) s_settings.weight = 80.0f;
}

void storage_load_settings(void) {
    if (!persist_exists(SETTINGS_PERSIST_KEY)) return;
    int bytes_read = persist_get_size(SETTINGS_PERSIST_KEY);
    if (bytes_read <= 0) return;

    #if defined(PBL_PLATFORM_APLITE)
    if ((size_t)bytes_read == sizeof(AppSettings)) {
        persist_read_data(SETTINGS_PERSIST_KEY, &s_settings, sizeof(AppSettings));
        sanitize_settings();
    }
    #else
    if ((size_t)bytes_read == sizeof(AppSettings)) {
        int bytes_to_read = bytes_read < (int)sizeof(AppSettings) ? bytes_read : (int)sizeof(AppSettings);
        persist_read_data(SETTINGS_PERSIST_KEY, &s_settings, bytes_to_read);
        sanitize_settings();
    } else if ((size_t)bytes_read == sizeof(AppSettingsV3)) {
        AppSettingsV3 legacy; persist_read_data(SETTINGS_PERSIST_KEY, &legacy, sizeof(AppSettingsV3));
        s_settings.weight = legacy.weight; s_settings.gender_constant = legacy.gender_constant;
        s_settings.use_metric_volume = legacy.use_metric_volume; s_settings.use_metric_weight = legacy.use_metric_weight;
        s_settings.theme_mode = legacy.theme_mode; s_settings.enable_portions = legacy.enable_portions;
        s_settings.right_handed_mode = legacy.right_handed_mode; s_settings.idle_timeout_mins = legacy.idle_timeout_mins;
        s_settings.target_bac = legacy.target_bac; s_settings.auto_exit = legacy.auto_exit; s_settings.region = legacy.region;
        sanitize_settings(); storage_save_settings();
    } else if ((size_t)bytes_read == sizeof(AppSettingsV2)) {
        AppSettingsV2 legacy; persist_read_data(SETTINGS_PERSIST_KEY, &legacy, sizeof(AppSettingsV2));
        s_settings.weight = legacy.weight; s_settings.gender_constant = legacy.gender_constant;
        s_settings.use_metric_volume = legacy.use_metric_volume; s_settings.use_metric_weight = legacy.use_metric_weight;
        s_settings.theme_mode = legacy.theme_mode; s_settings.enable_portions = legacy.enable_portions;
        s_settings.right_handed_mode = legacy.right_handed_mode; s_settings.idle_timeout_mins = legacy.idle_timeout_mins;
        s_settings.target_bac = legacy.target_bac; s_settings.auto_exit = legacy.auto_exit;
        sanitize_settings(); storage_save_settings();
    } else {
        AppSettingsV1 legacy; memset(&legacy, 0, sizeof(AppSettingsV1));
        int bytes_to_read = bytes_read < (int)sizeof(AppSettingsV1) ? bytes_read : (int)sizeof(AppSettingsV1);
        persist_read_data(SETTINGS_PERSIST_KEY, &legacy, bytes_to_read);
        s_settings.weight = legacy.weight; s_settings.gender_constant = legacy.gender_constant;
        s_settings.use_metric_volume = legacy.use_metric_volume; s_settings.use_metric_weight = legacy.use_metric_weight;
        s_settings.theme_mode = legacy.theme_mode; s_settings.enable_portions = legacy.enable_portions;
        s_settings.right_handed_mode = legacy.right_handed_mode; s_settings.auto_exit = legacy.auto_exit;
        sanitize_settings(); storage_save_settings();
    }
    #endif
}

void storage_save_settings(void) { persist_write_data(SETTINGS_PERSIST_KEY, &s_settings, sizeof(AppSettings)); }
AppSettings* storage_get_settings(void) { return &s_settings; }

static bool is_dark_theme_active(void) {
    if (s_settings.theme_mode == THEME_MODE_DARK) return true;
    if (s_settings.theme_mode == THEME_MODE_AUTO) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        return t && (t->tm_hour >= 18 || t->tm_hour < 6);
    }
    return false;
}

GColor theme_bg(void) { return is_dark_theme_active() ? GColorBlack : GColorWhite; }
GColor theme_text(void) { return is_dark_theme_active() ? GColorWhite : GColorBlack; }
GColor theme_highlight_bg(void) { return PBL_IF_COLOR_ELSE(GColorElectricUltramarine, theme_text()); }
GColor theme_highlight_text(void) { return PBL_IF_COLOR_ELSE(GColorWhite, theme_bg()); }

void storage_load_drinks(Drink* drinks, int* num_drinks) {
    // FIX: Allocate the max block upfront and NEVER free it during the session.
    if (!s_drinks) {
        s_drinks = malloc(sizeof(Drink) * MAX_DRINKS);
    }
    *num_drinks = 0; s_num_drinks = 0;

    if (persist_exists(NUM_DRINKS_PERSIST_KEY) && s_drinks) {
        int count = persist_read_int(NUM_DRINKS_PERSIST_KEY);

        if (count > 0 && persist_exists(DRINKS_PERSIST_KEY)) {
            int bytes_read = persist_get_size(DRINKS_PERSIST_KEY);
            int actual_persisted_count = 0;

            #if defined(PBL_PLATFORM_APLITE)
            if (bytes_read % sizeof(Drink) == 0) actual_persisted_count = bytes_read / sizeof(Drink);
            else actual_persisted_count = count;
            #else
            if (bytes_read % sizeof(Drink) == 0) actual_persisted_count = bytes_read / sizeof(Drink);
            else if (bytes_read % sizeof(DrinkV1) == 0) actual_persisted_count = bytes_read / sizeof(DrinkV1);
            else if (bytes_read % sizeof(DrinkV0) == 0) actual_persisted_count = bytes_read / sizeof(DrinkV0);
            else actual_persisted_count = count;
            #endif

            int load_count = actual_persisted_count > MAX_DRINKS ? MAX_DRINKS : actual_persisted_count;

            #if defined(PBL_PLATFORM_APLITE)
            if (bytes_read > 0 && (size_t)bytes_read == sizeof(Drink) * count) {
                if (count > MAX_DRINKS) {
                    Drink *temp = malloc(bytes_read);
                    if (temp) {
                        persist_read_data(DRINKS_PERSIST_KEY, temp, bytes_read);
                        for(int i = 0; i < load_count; i++) s_drinks[i] = temp[i];
                        free(temp);
                    }
                } else {
                    persist_read_data(DRINKS_PERSIST_KEY, s_drinks, bytes_read);
                }
                s_num_drinks = load_count;
            }
            #else
            if (bytes_read > 0 && (size_t)bytes_read == sizeof(DrinkV0) * count) {
                DrinkV0 *old_v0 = malloc(bytes_read);
                if (old_v0) {
                    persist_read_data(DRINKS_PERSIST_KEY, old_v0, bytes_read);
                    for(int i = 0; i < load_count; i++) {
                        s_drinks[i].timestamp = old_v0[i].timestamp; s_drinks[i].volume_ml = old_v0[i].volume_ml;
                        s_drinks[i].original_volume_ml = old_v0[i].volume_ml; s_drinks[i].abv = old_v0[i].abv;
                        s_drinks[i].shape = SHAPE_CUSTOM;
                    }
                    free(old_v0); s_num_drinks = load_count; storage_save_drinks(s_drinks, load_count);
                }
            } else if (bytes_read > 0 && (size_t)bytes_read == sizeof(DrinkV1) * count) {
                DrinkV1 *old_v1 = malloc(bytes_read);
                if (old_v1) {
                    persist_read_data(DRINKS_PERSIST_KEY, old_v1, bytes_read);
                    for(int i = 0; i < load_count; i++) {
                        s_drinks[i].timestamp = old_v1[i].timestamp; s_drinks[i].volume_ml = old_v1[i].volume_ml;
                        s_drinks[i].original_volume_ml = old_v1[i].volume_ml; s_drinks[i].abv = old_v1[i].abv;
                        s_drinks[i].shape = old_v1[i].shape;
                    }
                    free(old_v1); s_num_drinks = load_count; storage_save_drinks(s_drinks, load_count);
                }
            } else if (bytes_read > 0 && (size_t)bytes_read == sizeof(Drink) * count) {
                if (count > MAX_DRINKS) {
                    Drink *temp = malloc(bytes_read);
                    if (temp) {
                        persist_read_data(DRINKS_PERSIST_KEY, temp, bytes_read);
                        for(int i = 0; i < load_count; i++) s_drinks[i] = temp[i];
                        free(temp);
                    }
                } else {
                    persist_read_data(DRINKS_PERSIST_KEY, s_drinks, bytes_read);
                }
                s_num_drinks = load_count;
            }
            #endif

            bool is_corrupted = false;
            for(int i = 0; i < s_num_drinks; i++) {
                if (!(s_drinks[i].abv >= 0.0f && s_drinks[i].abv <= 1.0f) ||
                    !(s_drinks[i].volume_ml >= 0.0f && s_drinks[i].volume_ml <= 10000.0f)) {
                    is_corrupted = true; break;
                    }
            }

            if (is_corrupted) {
                storage_clear_drinks();
            } else {
                *num_drinks = s_num_drinks;
                if (drinks != NULL) { for(int i = 0; i < s_num_drinks; i++) drinks[i] = s_drinks[i]; }
            }
        }
    }
}

void storage_save_drinks(Drink* drinks, int num_drinks) {
    s_num_drinks = num_drinks;

    if (s_num_drinks > 0 && s_drinks) {
        // FIX: Removed `realloc`. Array stays MAX_DRINKS sized to protect heap health.
        persist_write_int(NUM_DRINKS_PERSIST_KEY, s_num_drinks);
        persist_write_data(DRINKS_PERSIST_KEY, s_drinks, sizeof(Drink) * s_num_drinks);
    } else {
        storage_clear_drinks();
    }
}

void storage_add_drink(Drink drink) {
    if (s_num_drinks < MAX_DRINKS) {
        if (!s_drinks) s_drinks = malloc(sizeof(Drink) * MAX_DRINKS);
        if (s_drinks) {
            s_drinks[s_num_drinks++] = drink;
            storage_save_drinks(s_drinks, s_num_drinks);
        }
    }
}

void storage_clear_drinks(void) {
    s_num_drinks = 0;
    // FIX: Do NOT free(s_drinks). Keep it pre-allocated for the entire session.
    persist_write_int(NUM_DRINKS_PERSIST_KEY, 0);
    persist_delete(DRINKS_PERSIST_KEY);
}

Drink* storage_get_drinks(void) { return s_drinks; }
int storage_get_num_drinks(void) { return s_num_drinks; }

void storage_deinit(void) {
    if (s_drinks) { free(s_drinks); s_drinks = NULL; }
}
