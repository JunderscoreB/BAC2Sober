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
    .dark_mode = false
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

GColor theme_bg(void) { return s_settings.dark_mode ? GColorBlack : GColorWhite; }
GColor theme_text(void) { return s_settings.dark_mode ? GColorWhite : GColorBlack; }

// Updated to a sleek blue/purple highlight
GColor theme_highlight_bg(void) { return PBL_IF_COLOR_ELSE(GColorElectricUltramarine, theme_text()); }
GColor theme_highlight_text(void) { return PBL_IF_COLOR_ELSE(GColorWhite, theme_bg()); }

void storage_load_drinks(Drink* drinks, int* num_drinks) {
    if (persist_exists(NUM_DRINKS_PERSIST_KEY)) {
        *num_drinks = persist_read_int(NUM_DRINKS_PERSIST_KEY);
        if (*num_drinks > 0 && persist_exists(DRINKS_PERSIST_KEY)) {
            persist_read_data(DRINKS_PERSIST_KEY, drinks, sizeof(Drink) * (*num_drinks));
        }
    } else {
        *num_drinks = 0;
    }

    s_num_drinks = *num_drinks;
    for(int i = 0; i < s_num_drinks; i++) {
        s_drinks[i] = drinks[i];
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
