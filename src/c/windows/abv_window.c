#include <pebble.h>
#include "abv_window.h"
#include "time_offset_menu.h"
#include "../core/storage.h"

static Window *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_abv_layer;

static float s_current_abv = 5.0f;
static float s_current_volume_ml = 0.0f;

static void update_abv_text(void) {
    static char s_buffer[16];

    // Safely round floating point to avoid precision drift (e.g., 5.09999 -> 5.1)
    int abv_tenths = (int)(s_current_abv * 10.0f + 0.5f);
    int abv_whole = abv_tenths / 10;
    int abv_decimal = abv_tenths % 10;

    snprintf(s_buffer, sizeof(s_buffer), "%d.%d%%", abv_whole, abv_decimal);
    text_layer_set_text(s_abv_layer, s_buffer);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    s_current_abv += 0.1f; // Changed from 0.5f to 0.1f
    if (s_current_abv > 20.0f) s_current_abv = 20.0f;
    update_abv_text();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_current_abv > 0.1f) s_current_abv -= 0.1f; // Changed from 0.5f to 0.1f
    else s_current_abv = 0.0f;
    update_abv_text();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    time_offset_menu_push(s_current_volume_ml, s_current_abv / 100.0f);
}

static void click_config_provider(void *context) {
    window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, up_click_handler);
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, down_click_handler);
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    // Apply the global Dark/Light mode theme
    window_set_background_color(window, theme_bg());

    s_title_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 40, bounds.size.w, 30));
    text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_title_layer, GColorClear);
    text_layer_set_text_color(s_title_layer, theme_text());
    text_layer_set_text(s_title_layer, "Adjust ABV");
    layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

    s_abv_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 10, bounds.size.w, 60));
    text_layer_set_font(s_abv_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
    text_layer_set_text_alignment(s_abv_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_abv_layer, GColorClear);
    text_layer_set_text_color(s_abv_layer, theme_text());
    layer_add_child(window_layer, text_layer_get_layer(s_abv_layer));

    update_abv_text();
}

static void window_unload(Window *window) {
    text_layer_destroy(s_title_layer);
    text_layer_destroy(s_abv_layer);
    window_destroy(s_window);
    s_window = NULL;
}

void abv_window_push(float volume_ml, float default_abv) {
    s_current_volume_ml = volume_ml;
    s_current_abv = default_abv;

    if(!s_window) {
        s_window = window_create();
        window_set_click_config_provider(s_window, click_config_provider);
        window_set_window_handlers(s_window, (WindowHandlers) {
            .load = window_load,
            .unload = window_unload,
        });
    }
    window_stack_push(s_window, true);
}
