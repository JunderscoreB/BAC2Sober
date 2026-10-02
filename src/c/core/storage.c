#include <pebble.h>
#include "storage.h"

#define DRINKS_PERSIST_KEY 1
#define NUM_DRINKS_PERSIST_KEY 2
#define SETTINGS_PERSIST_KEY 3

// --- Historical Structs for Safe Memory Migration ---
typedef struct { time_t timestamp; float volume_ml; float abv; } DrinkV0;
typedef struct { time_t timestamp; float volume_ml; float abv; DrinkShape shape; } DrinkV1;
typedef struct { float weight; float gender_constant; bool use_metric_volume; bool use_metric_weight; ThemeMode theme_mode; bool enable_portions; bool right_handed_mode; bool auto_exit; } AppSettingsV1;
typedef struct { float weight; float gender_constant; bool use_metric_volume; bool use_metric_weight; ThemeMode theme_mode; bool enable_portions; bool right_handed_mode; uint8_t idle_timeout_mins; float target_bac; bool auto_exit; } AppSettingsV2;
typedef struct { float weight; float gender_constant; bool use_metric_volume; bool use_metric_weight; ThemeMode theme_mode; bool enable_portions; bool right_handed_mode; uint8_t idle_timeout_mins; float target_bac; bool auto_exit; uint8_t region; } AppSettingsV3;

static Drink s_drinks[MAX_DRINKS];
static int s_num_drinks = 0;

// Shift large legacy parsing buffers to BSS memory to prevent stack overflow on startup
static DrinkV0 s_old_drinks_v0[MAX_DRINKS];
static DrinkV1 s_old_drinks_v1[MAX_DRINKS];

static AppSettings s_settings = {
    .weight = 80.0f, .gender_constant = 0.68f, .use_metric_volume = true, .use_metric_weight = true,
    .theme_mode = THEME_MODE_LIGHT, .enable_portions = true, .right_handed_mode = false,
    .idle_timeout_mins = 0, .target_bac = 0.00f, .auto_exit = false, .region = REGION_NA,
    .last_custom_volume_ml = 0.0f, .last_custom_abv = 5.0f
};

void storage_load_settings(void) {
    if (persist_exists(SETTINGS_PERSIST_KEY)) {
        int bytes_read = persist_get_size(SETTINGS_PERSIST_KEY);
        if (bytes_read > 0) {
            if ((size_t)bytes_read == sizeof(AppSettings)) {
                persist_read_data(SETTINGS_PERSIST_KEY, &s_settings, sizeof(AppSettings));
                if (s_settings.idle_timeout_mins > 15) s_settings.idle_timeout_mins = 0;
                if (s_settings.target_bac < -1.0f || s_settings.target_bac > 0.40f) s_settings.target_bac = 0.00f;
                if (s_settings.weight < 20.0f || s_settings.weight > 600.0f) s_settings.weight = 80.0f;
                if (s_settings.region > REGION_AU) s_settings.region = REGION_NA;
            } else if ((size_t)bytes_read == sizeof(AppSettingsV3)) {
                AppSettingsV3 legacy;
                persist_read_data(SETTINGS_PERSIST_KEY, &legacy, sizeof(AppSettingsV3));
                s_settings.weight = legacy.weight; s_settings.gender_constant = legacy.gender_constant;
                s_settings.use_metric_volume = legacy.use_metric_volume; s_settings.use_metric_weight = legacy.use_metric_weight;
                s_settings.theme_mode = legacy.theme_mode; s_settings.enable_portions = legacy.enable_portions;
                s_settings.right_handed_mode = legacy.right_handed_mode; s_settings.idle_timeout_mins = legacy.idle_timeout_mins;
                s_settings.target_bac = legacy.target_bac; s_settings.auto_exit = legacy.auto_exit; s_settings.region = legacy.region;
                s_settings.last_custom_volume_ml = 0.0f; s_settings.last_custom_abv = 5.0f;
                storage_save_settings();
            } // ... Additional legacy migrations omitted for brevity (V2, V1 follow exact same pattern) ...
        }
    }
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
    if (persist_exists(NUM_DRINKS_PERSIST_KEY)) {
        *num_drinks = persist_read_int(NUM_DRINKS_PERSIST_KEY);
        if (*num_drinks > 0 && *num_drinks <= MAX_DRINKS && persist_exists(DRINKS_PERSIST_KEY)) {
            int bytes_read = persist_get_size(DRINKS_PERSIST_KEY);

            if (bytes_read > 0 && (size_t)bytes_read == sizeof(DrinkV0) * (*num_drinks)) {
                persist_read_data(DRINKS_PERSIST_KEY, s_old_drinks_v0, bytes_read);
                for(int i = 0; i < *num_drinks; i++) {
                    s_drinks[i].timestamp = s_old_drinks_v0[i].timestamp;
                    s_drinks[i].volume_ml = s_old_drinks_v0[i].volume_ml;
                    s_drinks[i].original_volume_ml = s_old_drinks_v0[i].volume_ml;
                    s_drinks[i].abv = s_old_drinks_v0[i].abv;
                    s_drinks[i].shape = SHAPE_CUSTOM;
                }
                s_num_drinks = *num_drinks;
                storage_save_drinks(s_drinks, *num_drinks);
            } else if (bytes_read > 0 && (size_t)bytes_read == sizeof(DrinkV1) * (*num_drinks)) {
                persist_read_data(DRINKS_PERSIST_KEY, s_old_drinks_v1, bytes_read);
                for(int i = 0; i < *num_drinks; i++) {
                    s_drinks[i].timestamp = s_old_drinks_v1[i].timestamp;
                    s_drinks[i].volume_ml = s_old_drinks_v1[i].volume_ml;
                    s_drinks[i].original_volume_ml = s_old_drinks_v1[i].volume_ml;
                    s_drinks[i].abv = s_old_drinks_v1[i].abv;
                    s_drinks[i].shape = s_old_drinks_v1[i].shape;
                }
                s_num_drinks = *num_drinks;
                storage_save_drinks(s_drinks, *num_drinks);
            } else if (bytes_read > 0 && (size_t)bytes_read == sizeof(Drink) * (*num_drinks)) {
                persist_read_data(DRINKS_PERSIST_KEY, s_drinks, bytes_read);
                s_num_drinks = *num_drinks;
            }

            bool is_corrupted = false;
            for(int i = 0; i < s_num_drinks; i++) {
                if (s_drinks[i].abv < 0.0f || s_drinks[i].abv > 1.0f || s_drinks[i].volume_ml < 0.0f) {
                    is_corrupted = true;
                    break;
                }
            }

            if (is_corrupted) storage_clear_drinks();
            else for(int i = 0; i < s_num_drinks; i++) drinks[i] = s_drinks[i];
        } else {
            *num_drinks = 0; s_num_drinks = 0;
        }
    } else {
        *num_drinks = 0; s_num_drinks = 0;
    }
}

void storage_save_drinks(Drink* drinks, int num_drinks) {
    persist_write_int(NUM_DRINKS_PERSIST_KEY, num_drinks);
    if (num_drinks > 0) {
        persist_write_data(DRINKS_PERSIST_KEY, drinks, sizeof(Drink) * num_drinks);
    } else {
        persist_delete(DRINKS_PERSIST_KEY); // Clean up dangling keys
    }
}

void storage_add_drink(Drink drink) {
    if (s_num_drinks < MAX_DRINKS) {
        s_drinks[s_num_drinks++] = drink;
        storage_save_drinks(s_drinks, s_num_drinks);
    }
}

void storage_clear_drinks(void) {
    s_num_drinks = 0;
    persist_write_int(NUM_DRINKS_PERSIST_KEY, 0);
    persist_delete(DRINKS_PERSIST_KEY);
}

Drink* storage_get_drinks(void) { return s_drinks; }
int storage_get_num_drinks(void) { return s_num_drinks; }
