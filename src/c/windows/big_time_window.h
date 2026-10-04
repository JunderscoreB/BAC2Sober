#pragma once
#include <pebble.h>

#if defined(PBL_PLATFORM_APLITE)
void big_time_window_push(time_t target_time, const char* target_label);
void big_time_window_update(time_t target_time, const char* target_label);
#else
void big_time_window_push(time_t target_time, const char* target_label, GColor time_color);
void big_time_window_update(time_t target_time, const char* target_label, GColor time_color);
#endif

Window* big_time_window_get_window(void);
