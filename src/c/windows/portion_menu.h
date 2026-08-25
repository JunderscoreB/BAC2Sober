#pragma once
#include <pebble.h>
#include "../core/bac_math.h"

void portion_menu_push(float max_volume_ml, float current_volume_ml, float default_abv, DrinkShape shape, int edit_drink_idx);
void portion_menu_destroy_safe(void);