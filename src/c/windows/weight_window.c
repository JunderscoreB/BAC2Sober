#include <pebble.h>
#include "weight_window.h"
#include "../core/storage.h"

extern void app_reset_idle_timer(void);

static Window *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_weight_layer;

static float s_current_weight = 80.0f;
static bool s_is_metric = true;

static bool s_click_locked = false;
static void unlock_click(void *data) { s_click_locked = false; }

#if defined(PBL_TOUCH)
static uint16_t s_touch_repeat_count = 0;
#endif

static void update_weight_text(void) {
    static char s_buffer[16];
    if (s_is_metric) snprintf(s_buffer, sizeof(s_buffer), "%d kg", (int)s_current_weight);
    else snprintf(s_buffer, sizeof(s_buffer), "%d lbs", (int)s_current_weight);
    text_layer_set_text(s_weight_layer, s_buffer);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    app_reset_idle_timer();
    s_current_weight += 1.0f;
    if (s_is_metric && s_current_weight > 300.0f) s_current_weight = 300.0f;
    else if (!s_is_metric && s_current_weight > 600.0f) s_current_weight = 600.0f;
    update_weight_text();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    app_reset_idle_timer();
    s_current_weight -= 1.0f;
    if (s_is_metric && s_current_weight < 30.0f) s_current_weight = 30.0f;
    else if (!s_is_metric && s_current_weight < 60.0f) s_current_weight = 60.0f;
    update_weight_text();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_click_locked) return;
    s_click_locked = true; app_timer_register(300, unlock_click, NULL);
    app_reset_idle_timer();

    AppSettings *settings = storage_get_settings();
    settings->weight = s_current_weight;
    storage_save_settings();
    window_stack_pop(true);
}

static void click_config_provider(void *context) {
    window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, up_click_handler);
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, down_click_handler);
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

#ifdef PBL_TOUCH
static int16_t s_touch_start_x = 0; static int16_t s_touch_start_y = 0;
static int16_t s_touch_last_y = 0; static bool s_is_drag = false;
static AppTimer *s_touch_hold_timer = NULL;
static int16_t s_current_touch_y = -1;

static void touch_hold_timer_cb(void *data) {
    s_touch_hold_timer = NULL;
    if (s_current_touch_y < 0) return;

    Layer *window_layer = window_get_root_layer(s_window);
    GRect bounds = layer_get_bounds(window_layer);

    bool triggered = false;
    if (s_current_touch_y < bounds.size.h / 3) {
        s_touch_repeat_count++; up_click_handler(NULL, NULL); triggered = true;
    } else if (s_current_touch_y > (bounds.size.h * 2) / 3) {
        s_touch_repeat_count++; down_click_handler(NULL, NULL); triggered = true;
    }

    if (triggered) {
        s_touch_hold_timer = app_timer_register(150, touch_hold_timer_cb, NULL);
    }
}

static void touch_handler(const TouchEvent *event, void *context) {
    app_reset_idle_timer();
    if (event->type == TouchEvent_Touchdown) {
        s_touch_start_x = event->x; s_touch_start_y = event->y; s_touch_last_y = event->y;
        s_current_touch_y = event->y;
        s_touch_repeat_count = 0; s_is_drag = false;
        if (s_touch_hold_timer) { app_timer_cancel(s_touch_hold_timer); s_touch_hold_timer = NULL; }
    } else if (event->type == TouchEvent_PositionUpdate) {
        s_current_touch_y = event->y;
        if (!s_is_drag && abs(event->y - s_touch_start_y) > 10) s_is_drag = true;
        if (s_is_drag) {
            int16_t delta = event->y - s_touch_last_y;
            if (delta < -15) { s_touch_repeat_count++; up_click_handler(NULL, NULL); s_touch_last_y = event->y; }
            else if (delta > 15) { s_touch_repeat_count++; down_click_handler(NULL, NULL); s_touch_last_y = event->y; }

            Layer *window_layer = window_get_root_layer(s_window);
            GRect bounds = layer_get_bounds(window_layer);
            if (!s_touch_hold_timer && (event->y < bounds.size.h / 3 || event->y > (bounds.size.h * 2) / 3)) {
                s_touch_hold_timer = app_timer_register(150, touch_hold_timer_cb, NULL);
            } else if (s_touch_hold_timer && event->y >= bounds.size.h / 3 && event->y <= (bounds.size.h * 2) / 3) {
                app_timer_cancel(s_touch_hold_timer); s_touch_hold_timer = NULL;
            }
        }
    } else if (event->type == TouchEvent_Liftoff) {
        s_current_touch_y = -1;
        if (s_touch_hold_timer) { app_timer_cancel(s_touch_hold_timer); s_touch_hold_timer = NULL; }

        int16_t dx = event->x - s_touch_start_x; int16_t dy = event->y - s_touch_start_y;
        if (abs(dx) > 40 && abs(dx) > abs(dy)) {
            AppSettings *settings = storage_get_settings();
            bool is_back = settings->right_handed_mode ? (dx > 40) : (dx < -40);
            if (is_back) { window_stack_pop(true); return; }
        }
        if (!s_is_drag) select_click_handler(NULL, NULL);
    }
}
#endif

static void window_appear(Window *window) {
    s_click_locked = false;
    app_reset_idle_timer();
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

static void oom_pop_callback(void *data) { window_stack_pop(false); }

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window); GRect bounds = layer_get_bounds(window_layer);
    window_set_background_color(window, theme_bg());

    int title_y = bounds.size.h / 2 - 40;
    int val_y = bounds.size.h / 2 - 10;
    GFont title_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont val_font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);

    #if !defined(PBL_PLATFORM_APLITE)
    PreferredContentSize size = preferred_content_size();
    if (size == PreferredContentSizeLarge || size == PreferredContentSizeExtraLarge) {
        title_y = bounds.size.h / 2 - 45; val_y = bounds.size.h / 2 - 15;
        title_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
        val_font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
    } else if (size == PreferredContentSizeSmall) {
        title_y = bounds.size.h / 2 - 35; val_y = bounds.size.h / 2 - 5;
        title_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
        val_font = fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
    }
    #endif

    s_title_layer = text_layer_create(GRect(0, title_y, bounds.size.w, 30));
    s_weight_layer = text_layer_create(GRect(0, val_y, bounds.size.w, 60));

    if (!s_title_layer || !s_weight_layer) {
        app_timer_register(10, oom_pop_callback, NULL);
        return;
    }

    text_layer_set_font(s_title_layer, title_font);
    text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_title_layer, GColorClear);
    text_layer_set_text_color(s_title_layer, theme_text());
    text_layer_set_text(s_title_layer, "Adjust Weight");
    layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

    text_layer_set_font(s_weight_layer, val_font);
    text_layer_set_text_alignment(s_weight_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_weight_layer, GColorClear);
    text_layer_set_text_color(s_weight_layer, theme_text());
    layer_add_child(window_layer, text_layer_get_layer(s_weight_layer));

    update_weight_text();
}

static void destroy_window_cb(void *data) { window_destroy((Window*)data); }

static void window_unload(Window *window) {
    if (s_title_layer) { text_layer_destroy(s_title_layer); s_title_layer = NULL; }
    if (s_weight_layer) { text_layer_destroy(s_weight_layer); s_weight_layer = NULL; }
    if (window == s_window) {
        s_window = NULL;
        app_timer_register(50, destroy_window_cb, window);
    }
}

void weight_window_push(void) {
    AppSettings *settings = storage_get_settings();
    s_current_weight = settings->weight; s_is_metric = settings->use_metric_weight;

    if(!s_window) {
        s_window = window_create();
        if (!s_window) return;
        window_set_click_config_provider(s_window, click_config_provider);
        window_set_window_handlers(s_window, (WindowHandlers) {
            .load = window_load, .appear = window_appear,
            .disappear = window_disappear, .unload = window_unload,
        });
    }
    if (!window_stack_contains_window(s_window)) window_stack_push(s_window, true);
}
