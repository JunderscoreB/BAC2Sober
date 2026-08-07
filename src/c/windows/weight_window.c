#include <pebble.h>
#include "weight_window.h"
#include "../core/storage.h"

static Window *s_window;
static TextLayer *s_weight_layer;

static void update_weight_text(void) {
    static char s_buffer[16];
    AppSettings *settings = storage_get_settings();
    
    if (settings->use_metric_weight) {
        snprintf(s_buffer, sizeof(s_buffer), "%d kg", (int)settings->weight);
    } else {
        snprintf(s_buffer, sizeof(s_buffer), "%d lbs", (int)settings->weight);
    }
    text_layer_set_text(s_weight_layer, s_buffer);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    AppSettings *settings = storage_get_settings();
    settings->weight += 1.0f;
    update_weight_text();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    AppSettings *settings = storage_get_settings();
    if (settings->use_metric_weight && settings->weight > 20.0f) settings->weight -= 1.0f;
    else if (!settings->use_metric_weight && settings->weight > 44.0f) settings->weight -= 1.0f;
    update_weight_text();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    storage_save_settings();
    window_stack_pop(true);
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

    s_weight_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 30, bounds.size.w, 60));
    text_layer_set_font(s_weight_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
    text_layer_set_text_alignment(s_weight_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_weight_layer, GColorClear);
    text_layer_set_text_color(s_weight_layer, theme_text());
    layer_add_child(window_layer, text_layer_get_layer(s_weight_layer));
    
    update_weight_text();
}

static void window_unload(Window *window) {
    text_layer_destroy(s_weight_layer);
    window_destroy(s_window);
    s_window = NULL;
}

void weight_window_push(void) {
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