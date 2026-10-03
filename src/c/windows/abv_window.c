#include <pebble.h>
#include "abv_window.h"
#include "time_offset_menu.h"
#include "../core/storage.h"

extern void app_reset_idle_timer(void);
extern void wizard_advance(void);

extern int g_wizard_next_step;
extern float g_wizard_vol;
extern float g_wizard_orig_vol;
extern float g_wizard_abv;
extern DrinkShape g_wizard_shape;

static Window *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_abv_layer;

static float s_current_abv = 5.0f;
static float s_current_volume_ml = 0.0f;
static float s_original_volume_ml = 0.0f;
static DrinkShape s_shape;

static bool s_click_locked = false;
static void unlock_click(void *data) { s_click_locked = false; }

#if defined(PBL_TOUCH)
static uint16_t s_touch_repeat_count = 0;
#endif

static void update_abv_text(void) {
    static char s_buffer[16];
    int abv_tenths = (int)(s_current_abv * 10.0f + 0.5f);
    snprintf(s_buffer, sizeof(s_buffer), "%d.%d%%", abv_tenths / 10, abv_tenths % 10);
    text_layer_set_text(s_abv_layer, s_buffer);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    app_reset_idle_timer();
    uint16_t repeats = 0;
    if (recognizer) repeats = click_number_of_clicks_counted(recognizer);
    #if defined(PBL_TOUCH)
    else repeats = s_touch_repeat_count;
    #endif

    float step = (repeats > 11) ? 1.0f : 0.1f;
    s_current_abv += step;
    if (s_current_abv > 75.0f) s_current_abv = 75.0f;
    update_abv_text();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    app_reset_idle_timer();
    uint16_t repeats = 0;
    if (recognizer) repeats = click_number_of_clicks_counted(recognizer);
    #if defined(PBL_TOUCH)
    else repeats = s_touch_repeat_count;
    #endif

    float step = (repeats > 11) ? 1.0f : 0.1f;
    if (s_current_abv > step) s_current_abv -= step; else s_current_abv = 0.0f;
    update_abv_text();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_click_locked) return;
    s_click_locked = true; app_timer_register(300, unlock_click, NULL);
    app_reset_idle_timer();

    if (s_shape == SHAPE_CUSTOM) {
        AppSettings *settings = storage_get_settings();
        settings->last_custom_volume_ml = s_current_volume_ml;
        settings->last_custom_abv = s_current_abv;
        storage_save_settings();
    }

    g_wizard_abv = s_current_abv;
    g_wizard_next_step = 3;
    wizard_advance();
}

static void click_config_provider(void *context) {
    window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, up_click_handler);
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, down_click_handler);
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

#if defined(PBL_TOUCH)
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
    #if defined(PBL_TOUCH)
    if (touch_service_is_enabled()) touch_service_subscribe(touch_handler, NULL);
    #endif
}

static void window_disappear(Window *window) {
    #if defined(PBL_TOUCH)
    if (touch_service_is_enabled()) touch_service_unsubscribe();
    #endif
}

static void oom_pop_callback(void *data) { window_stack_pop(false); }

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window); GRect bounds = layer_get_bounds(window_layer);
    window_set_background_color(window, theme_bg());

    s_title_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 40, bounds.size.w, 30));
    s_abv_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 10, bounds.size.w, 60));

    if (!s_title_layer || !s_abv_layer) {
        app_timer_register(10, oom_pop_callback, NULL);
        return;
    }

    text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_title_layer, GColorClear);
    text_layer_set_text_color(s_title_layer, theme_text());
    text_layer_set_text(s_title_layer, "Adjust ABV");
    layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

    text_layer_set_font(s_abv_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
    text_layer_set_text_alignment(s_abv_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_abv_layer, GColorClear);
    text_layer_set_text_color(s_abv_layer, theme_text());
    layer_add_child(window_layer, text_layer_get_layer(s_abv_layer));

    update_abv_text();
}

static void destroy_window_cb(void *data) { window_destroy((Window*)data); }

static void window_unload(Window *window) {
    if (s_title_layer) { text_layer_destroy(s_title_layer); s_title_layer = NULL; }
    if (s_abv_layer) { text_layer_destroy(s_abv_layer); s_abv_layer = NULL; }
    if (window == s_window) {
        s_window = NULL;
        app_timer_register(50, destroy_window_cb, window);
    }
}

void abv_window_push(float volume_ml, float original_volume_ml, float default_abv, DrinkShape shape) {
    s_current_volume_ml = volume_ml; s_original_volume_ml = original_volume_ml;
    s_current_abv = default_abv; s_shape = shape;

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
