#pragma once
#include "bac_math.h"

#define MAX_DRINKS 20

// Ensure exactly 1 byte to maintain binary compatibility with the old "bool dark_mode" save files
typedef uint8_t ThemeMode;
#define THEME_MODE_LIGHT 0
#define THEME_MODE_DARK  1
#define THEME_MODE_AUTO  2

typedef struct {
    float weight; 
    float gender_constant;
    bool use_metric_volume;
    bool use_metric_weight;
    ThemeMode theme_mode; 
    bool enable_portions; // NEW: Toggle to skip the portion UI
} AppSettings;

void storage_load_drinks(Drink* drinks, int* num_drinks);
void storage_save_drinks(Drink* drinks, int num_drinks);
void storage_add_drink(Drink drink);
void storage_clear_drinks(void);
Drink* storage_get_drinks(void);
int storage_get_num_drinks(void);

void storage_load_settings(void);
void storage_save_settings(void);
AppSettings* storage_get_settings(void);

// Theme Engine Accessors
GColor theme_bg(void);
GColor theme_text(void);
GColor theme_highlight_bg(void);
GColor theme_highlight_text(void);