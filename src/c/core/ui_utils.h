#pragma once
#include <pebble.h>

int16_t ui_get_dynamic_cell_height(void);
void ui_draw_dynamic_menu_cell(GContext *ctx, const Layer *cell_layer, const char *title, const char *subtitle, bool is_selected, int marquee_offset);
