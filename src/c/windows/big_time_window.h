#pragma once
#include <pebble.h>

void big_time_window_push(time_t target_time, const char* target_label, GColor time_color);
void big_time_window_update(time_t target_time, const char* target_label, GColor time_color);
Window* big_time_window_get_window(void);