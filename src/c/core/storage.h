#pragma once
#include "bac_math.h"

#if defined(PBL_PLATFORM_APLITE)
#define MAX_DRINKS 15
#else
#define MAX_DRINKS 50
#endif

typedef uint8_t ThemeMode;
#define THEME_MODE_LIGHT 0
#define THEME_MODE_DARK  1
#define THEME_MODE_AUTO  2

#if !defined(PBL_PLATFORM_APLITE)
#define REGION_NA 0
#define REGION_UK 1
#define REGION_EU 2
#define REGION_AU 3
#endif

typedef struct {
    float weight;
    float gender_constant;
    bool use_metric_volume;
    bool use_metric_weight;
    ThemeMode theme_mode;
    bool enable_portions;
    #if !defined(PBL_PLATFORM_APLITE)
    bool right_handed_mode;
    #endif
    uint8_t idle_timeout_mins;
    float target_bac;
    bool auto_exit;
    #if !defined(PBL_PLATFORM_APLITE)
    uint8_t region;
    #endif
    float last_custom_volume_ml;
    float last_custom_abv;
} AppSettings;

#if defined(PBL_PLATFORM_APLITE)
void storage_load_drinks(void);
void storage_drop_oldest_drink(void);
#else
void storage_load_drinks(Drink* drinks, int* num_drinks);
void storage_add_drink_overwrite(Drink drink);
void storage_deinit(void);
#endif

void storage_save_drinks(Drink* drinks, int num_drinks);
bool storage_add_drink(Drink drink);
void storage_clear_drinks(void);
Drink* storage_get_drinks(void);
int storage_get_num_drinks(void);

void storage_load_settings(void);
void storage_save_settings(void);
AppSettings* storage_get_settings(void);

GColor theme_bg(void);
GColor theme_text(void);
GColor theme_highlight_bg(void);
GColor theme_highlight_text(void);
