#include <pebble.h>
#include "storage.h"

#define DRINKS_PERSIST_KEY 1
#define NUM_DRINKS_PERSIST_KEY 2
#define SETTINGS_PERSIST_KEY 3

static Drink s_drinks[MAX_DRINKS];
static int s_num_drinks = 0;

static AppSettings s_settings = {
    .weight = 80.0f, 
    .gender_constant = 0.68f, 
    .use_metric_volume = true,
    .use_metric_weight = true, 
    .theme_mode = THEME_MODE_LIGHT,
    .enable_portions = true
};

void storage_load_settings(void) {
    if (persist_exists(SETTINGS_PERSIST_KEY)) {
        persist_read_data(SETTINGS_PERSIST_KEY, &s_settings, sizeof(AppSettings));
    }
}

void storage_save_settings(void) {
    persist_write_data(SETTINGS_PERSIST_KEY, &s_settings, sizeof(AppSettings));
}

AppSettings* storage_get_settings(void) { return &s_settings; }

static bool is_dark_theme_active(void) {
    if (s_settings.theme_mode == THEME_MODE_DARK) return true;
    if (s_settings.theme_mode == THEME_MODE_AUTO) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        return (t->tm_hour >= 18 || t->tm_hour < 6);
    }
    return false;
}

GColor theme_bg(void) { return is_dark_theme_active() ? GColorBlack : GColorWhite; }
GColor theme_text(void) { return is_dark_theme_active() ? GColorWhite : GColorBlack; }
GColor theme_highlight_bg(void) { return PBL_IF_COLOR_ELSE(GColorElectricUltramarine, theme_text()); }
GColor theme_highlight_text(void) { return PBL_IF_COLOR_ELSE(GColorWhite, theme_bg()); }

// --- Safe Data Migration ---
void storage_load_drinks(Drink* drinks, int* num_drinks) {
    if (persist_exists(NUM_DRINKS_PERSIST_KEY)) {
        *num_drinks = persist_read_int(NUM_DRINKS_PERSIST_KEY);
        if (*num_drinks > 0 && persist_exists(DRINKS_PERSIST_KEY)) {
            
            // Check byte boundaries to detect legacy saves safely
            int bytes_read = persist_get_size(DRINKS_PERSIST_KEY);
            // Verify bytes_read is > 0 before casting to size_t to resolve signedness warning
            if (bytes_read > 0 && (size_t)bytes_read == sizeof(OldDrink) * (*num_drinks)) {
                OldDrink old_drinks[MAX_DRINKS];
                persist_read_data(DRINKS_PERSIST_KEY, old_drinks, sizeof(OldDrink) * (*num_drinks));
                for(int i = 0; i < *num_drinks; i++) {
                    s_drinks[i].timestamp = old_drinks[i].timestamp;
                    s_drinks[i].volume_ml = old_drinks[i].volume_ml;
                    s_drinks[i].original_volume_ml = old_drinks[i].volume_ml;
                    s_drinks[i].abv = old_drinks[i].abv;
                    s_drinks[i].shape = SHAPE_CUSTOM;
                    drinks[i] = s_drinks[i];
                }
                storage_save_drinks(s_drinks, *num_drinks); // Save new format immediately
            } else {
                // Standard modern load
                persist_read_data(DRINKS_PERSIST_KEY, s_drinks, sizeof(Drink) * (*num_drinks));
                for(int i = 0; i < *num_drinks; i++) {
                    drinks[i] = s_drinks[i];
                }
            }
        }
    } else {
        *num_drinks = 0;
    }
}

void storage_save_drinks(Drink* drinks, int num_drinks) {
    persist_write_int(NUM_DRINKS_PERSIST_KEY, num_drinks);
    if (num_drinks > 0) {
        persist_write_data(DRINKS_PERSIST_KEY, drinks, sizeof(Drink) * num_drinks);
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