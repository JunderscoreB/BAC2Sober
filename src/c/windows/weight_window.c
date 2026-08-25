#include <pebble.h>
#include "weight_window.h"
#include "../core/storage.h"

static Window *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_weight_layer;

static float s_current_weight = 80.0f;
static bool s_is_metric = true;

static void update_weight_text(void) {
    static char s_buffer[16];
    if (s_is_metric) {
        snprintf(s_buffer, sizeof(s_buffer), "%d kg", (int)s_current_weight);
    } else {
        snprintf(s_buffer, sizeof(s_buffer), "%d lbs", (int)s_current_weight);
    }
    text_layer_set_text(s_weight_layer, s_buffer);
}

// --- Interaction Handlers (Buttons & Touch) ---
static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    s_current_weight += 1.0f;
    
    // Set reasonable upper bounds based on the chosen unit
    if (s_is_metric && s_current_weight > 300.0f) s_current_weight = 300.0f;
    else if (!s_is_metric && s_current_weight > 600.0f) s_current_weight = 600.0f;
    
    update_weight_text();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    s_current_weight -= 1.0f;
    
    // Set reasonable lower bounds based on the chosen unit
    if (s_is_metric && s_current_weight < 30.0f) s_current_weight = 30.0f;
    else if (!s_is_metric && s_current_weight < 60.0f) s_current_weight = 60.0f;
    
    update_weight_text();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    // Save the new weight setting globally
    AppSettings *settings = storage_get_settings();
    settings->weight = s_current_weight;
    storage_save_settings();
    
    // Return to the Settings Menu
    window_stack_pop(true);
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
            if (delta < -15) { // Swipe up
                up_click_handler(NULL, NULL);
                s_touch_last_y = event->y;
            } else if (delta > 15) { // Swipe down
                down_click_handler(NULL, NULL);
                s_touch_last_y = event->y;
            }
        }
    } else if (event->type == TouchEvent_Liftoff) {
        if (!s_is_drag) select_click_handler(NULL, NULL); // Strong tap saves and exits!
    }
}
#endif

static void window_appear(Window *window) {
    #ifdef PBL_TOUCH
    if (touch_service_is_enabled()) {
        touch_service_subscribe(touch_handler, NULL);
    }
    #endif
}

static void window_disappear(Window *window) {
    #ifdef PBL_TOUCH
    if (touch_service_is_enabled()) {
        touch_service_unsubscribe();
    }
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
    text_layer_set_text(s_title_layer, "Adjust Weight");
    layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

    s_weight_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 10, bounds.size.w, 60));
    text_layer_set_font(s_weight_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
    text_layer_set_text_alignment(s_weight_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_weight_layer, GColorClear);
    text_layer_set_text_color(s_weight_layer, theme_text());
    layer_add_child(window_layer, text_layer_get_layer(s_weight_layer));
    
    update_weight_text();
}

static void window_unload(Window *window) {
    text_layer_destroy(s_title_layer);
    text_layer_destroy(s_weight_layer);
    window_destroy(s_window);
    s_window = NULL;
}

void weight_window_push(void) {
    // Load the user's established preferences dynamically before setting up the UI
    AppSettings *settings = storage_get_settings();
    s_current_weight = settings->weight;
    s_is_metric = settings->use_metric_weight;

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