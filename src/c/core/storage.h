#pragma once
#include "bac_math.h"

#define MAX_DRINKS 20

typedef struct {
    float weight;
    float gender_constant;
    bool use_metric_volume;
    bool use_metric_weight;
    bool dark_mode;
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
