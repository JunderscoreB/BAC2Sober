#include <pebble.h>
#include "custom_volume_window.h"
#include "abv_window.h"
#include "../core/storage.h"

static Window *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_volume_layer;

static float s_current_volume = 355.0f;
static float s_default_abv = 5.0f;

static void update_volume_text(void) {
    static char s_buffer[16];
    AppSettings *settings = storage_get_settings();
    if (settings->use_metric_volume) {
        snprintf(s_buffer, sizeof(s_buffer), "%dml", (int)s_current_volume);
    } else {
        snprintf(s_buffer, sizeof(s_buffer), "%doz", (int)(s_current_volume / 29.5735f));
    }
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
    // Provide both volumes, default abv, and default SHAPE_CUSTOM
    abv_window_push(s_current_volume, s_current_volume, s_default_abv, SHAPE_CUSTOM);
}

static void click_config_provider(void *context) {
    window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, up_click_handler);
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, down_click_handler);
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

#ifdef PBL_TOUCH
static int16_t s_touch_start_y = 0;
static int16_t s_touch_last_y = 0;
static bool s_is_drag = false;

static void touch_handler(const TouchEvent *event, void *context) {
    if (event->type == TouchEvent_Touchdown) {
        s_touch_start_y = event->y;
        s_touch_last_y = event->y;
        s_is_drag = false;
    } else if (event->type == TouchEvent_PositionUpdate) {
        if (!s_is_drag && abs(event->y - s_touch_start_y) > 10) s_is_drag = true;
        
        if (s_is_drag) {
            int16_t delta = event->y - s_touch_last_y;
            if (delta < -15) { 
                up_click_handler(NULL, NULL);
                s_touch_last_y = event->y;
            } else if (delta > 15) { 
                down_click_handler(NULL, NULL);
                s_touch_last_y = event->y;
            }
        }
    } else if (event->type == TouchEvent_Liftoff) {
        if (!s_is_drag) select_click_handler(NULL, NULL); 
    }
}
#endif

static void window_appear(Window *window) {
    #ifdef PBL_TOUCH
    if (touch_service_is_enabled()) touch_service_subscribe(touch_handler, NULL);
    #endif
}

static void window_disappear(Window *window) {
    #ifdef PBL_TOUCH
    if (touch_service_is_enabled()) touch_service_unsubscribe();
    #endif
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
    text_layer_set_text(s_title_layer, "Adjust Volume");
    layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

    s_volume_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 10, bounds.size.w, 60));
    text_layer_set_font(s_volume_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
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

void custom_volume_window_push(float default_volume, float default_abv) {
    s_current_volume = default_volume;
    s_default_abv = default_abv;

    if(!s_window) {
        s_window = window_create();
        window_set_click_config_provider(s_window, click_config_provider);
        window_set_window_handlers(s_window, (WindowHandlers) {
            .load = window_load,
            .appear = window_appear,
            .disappear = window_disappear,
            .unload = window_unload,
        });
    }
    window_stack_push(s_window, true);
}

void custom_volume_window_destroy_safe(void) {
    if (s_window && window_stack_contains_window(s_window)) {
        window_stack_remove(s_window, false);
    }
}