#include <pebble.h>
#include "custom_volume_window.h"
#include "abv_window.h"
#include "portion_menu.h"
#include "../core/storage.h"

static Window *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_volume_layer;

static float s_current_volume = 355.0f;
static float s_default_abv = 5.0f;

static void update_volume_text(void) {
    static char s_buffer[32];
    snprintf(s_buffer, sizeof(s_buffer), "%dml (%doz)", (int)s_current_volume, (int)(s_current_volume / 29.5735f));
    text_layer_set_text(s_volume_layer, s_buffer);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    s_current_volume += 10.0f;
    if (s_current_volume > 5000.0f) s_current_volume = 5000.0f;
    update_volume_text();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    s_current_volume -= 10.0f;
    if (s_current_volume < 10.0f) s_current_volume = 10.0f;
    update_volume_text();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    // For completely custom volumes, we will route into the portion screen using the custom (simple cylinder) shape.
    portion_menu_push(s_current_volume, s_default_abv, SHAPE_CUSTOM);
}

static void click_config_provider(void *context) {
    window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, up_click_handler);
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, down_click_handler);
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    window_set_background_color(window, theme_bg());

    s_title_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 40, bounds.size.w, 30));
    text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_title_layer, GColorClear);
    text_layer_set_text_color(s_title_layer, theme_text());
    layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

    text_layer_set_text(s_title_layer, "Custom Volume");

    s_volume_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 10, bounds.size.w, 40));
    text_layer_set_font(s_volume_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
    text_layer_set_text_alignment(s_volume_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_volume_layer, GColorClear);
    text_layer_set_text_color(s_volume_layer, theme_text());
    layer_add_child(window_layer, text_layer_get_layer(s_volume_layer));
    
    update_volume_text();
}

static void window_unload(Window *window) {
    text_layer_destroy(s_title_layer);
    text_layer_destroy(s_volume_layer);
    window_destroy(s_window);
    s_window = NULL;
}

void custom_volume_window_push(float default_volume_ml, float default_abv) {
    s_current_volume = default_volume_ml;
    s_default_abv = default_abv;

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