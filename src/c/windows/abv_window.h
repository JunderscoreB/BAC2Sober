#pragma once
#include <pebble.h>
#include "../core/bac_math.h"
void abv_window_push(float volume_ml, float original_volume_ml, float default_abv, DrinkShape shape);
void abv_window_destroy_safe(void);