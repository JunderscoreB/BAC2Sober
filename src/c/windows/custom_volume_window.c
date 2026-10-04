// ============================================================================
// File: src/windows/custom_volume_window.c
// ============================================================================
#include <pebble.h>
#include "custom_volume_window.h"
#include "abv_window.h"
#include "../core/storage.h"

extern void app_reset_idle_timer(void);

#if !defined(PBL_PLATFORM_APLITE)
extern void wizard_advance(void);
extern int g_wizard_next_step;
extern float g_wizard_vol;
extern float g_wizard_orig_vol;
extern float g_wizard_abv;
extern DrinkShape g_wizard_shape;
#endif

static Window *s_window;

#if defined(PBL_PLATFORM_APLITE)
static Layer *s_canvas_layer;
#else
static TextLayer *s_title_layer;
static TextLayer *s_volume_layer;
static bool s_click_locked = false;
static void unlock_click(void *data) { s_click_locked = false; }
static void oom_pop_callback(void *data) { window_stack_pop(false); }
#endif

void custom_volume_window_remove_from_stack(void) {
    if (s_window && window_stack_contains_window(s_window)) {
        window_stack_remove(s_window, false);
    }
}

static float s_current_volume = 355.0f;
static float s_default_abv = 5.0f;

#if defined(PBL_TOUCH)
static uint16_t s_touch_repeat_count = 0;
#endif

#if defined(PBL_PLATFORM_APLITE)
static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    graphics_context_set_text_color(ctx, theme_text());

    char buffer[24];
    AppSettings *settings = storage_get_settings();

    float oz = (s_current_volume / 29.5735f) + 0.05f;
    int oz_w = (int)oz;
    int oz_d = (int)(oz * 10.0f) % 10;

    if (settings->use_metric_volume) {
        snprintf(buffer, sizeof(buffer), "%d ml", (int)s_current_volume);
    } else {
        snprintf(buffer, sizeof(buffer), "%d.%d oz", oz_w, oz_d);
    }

    int center_y = bounds.size.h / 2;

    graphics_draw_text(ctx, "Adjust Volume", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(0, center_y - 40, bounds.size.w, 30),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    graphics_draw_text(ctx, buffer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
                       GRect(0, center_y - 10, bounds.size.w, 60),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}
#else
static void update_volume_text(void) {
    static char s_buffer[24];
    AppSettings *settings = storage_get_settings();

    int vol_ml = (int)(s_current_volume + 0.5f);
    int oz_tenths = (vol_ml * 10000 + 14786) / 29573;

    if (settings->use_metric_volume) {
        snprintf(s_buffer, sizeof(s_buffer), "%d ml", vol_ml);
    } else {
        snprintf(s_buffer, sizeof(s_buffer), "%d.%d oz", oz_tenths / 10, oz_tenths % 10);
    }
    text_layer_set_text(s_volume_layer, s_buffer);
}
#endif

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    app_reset_idle_timer();
    s_current_volume += 10.0f;
    if (s_current_volume > 5000.0f) s_current_volume = 5000.0f;

    #if defined(PBL_PLATFORM_APLITE)
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
    #else
    update_volume_text();
    #endif
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    app_reset_idle_timer();
    s_current_volume -= 10.0f;
    if (s_current_volume < 10.0f) s_current_volume = 10.0f;

    #if defined(PBL_PLATFORM_APLITE)
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
    #else
    update_volume_text();
    #endif
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    #if !defined(PBL_PLATFORM_APLITE)
    if (s_click_locked) return;
    s_click_locked = true; app_timer_register(300, unlock_click, NULL);
    #endif
    app_reset_idle_timer();

    #if defined(PBL_PLATFORM_APLITE)
    abv_window_push(s_current_volume, s_current_volume, s_default_abv, SHAPE_CUSTOM);
    #else
    g_wizard_vol = s_current_volume;
    g_wizard_orig_vol = s_current_volume;
    g_wizard_abv = s_default_abv;
    g_wizard_shape = SHAPE_CUSTOM;
    g_wizard_next_step = 2;
    wizard_advance();
    #endif
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
    if (s_current_touch_y < 0 || !s_window) return;

    Layer *window_layer = window_get_root_layer(s_window);
    if (!window_layer) return;
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

            if (s_window) {
                Layer *window_layer = window_get_root_layer(s_window);
                if (window_layer) {
                    GRect bounds = layer_get_bounds(window_layer);
                    if (!s_touch_hold_timer && (event->y < bounds.size.h / 3 || event->y > (bounds.size.h * 2) / 3)) {
                        s_touch_hold_timer = app_timer_register(150, touch_hold_timer_cb, NULL);
                    } else if (s_touch_hold_timer && event->y >= bounds.size.h / 3 && event->y <= (bounds.size.h * 2) / 3) {
                        app_timer_cancel(s_touch_hold_timer); s_touch_hold_timer = NULL;
                    }
                }
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
    #if !defined(PBL_PLATFORM_APLITE)
    s_click_locked = false;
    #endif
    app_reset_idle_timer();
    #if defined(PBL_TOUCH)
    if (touch_service_is_enabled()) touch_service_subscribe(touch_handler, NULL);
    #endif
}

static void window_disappear(Window *window) {
    #if defined(PBL_TOUCH)
    if (touch_service_is_enabled()) touch_service_unsubscribe();
    if (s_touch_hold_timer) { app_timer_cancel(s_touch_hold_timer); s_touch_hold_timer = NULL; }
    #endif
}

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window); GRect bounds = layer_get_bounds(window_layer);
    window_set_background_color(window, theme_bg());

    #if defined(PBL_PLATFORM_APLITE)
    s_canvas_layer = layer_create(bounds);
    if (!s_canvas_layer) {
        APP_LOG(APP_LOG_LEVEL_ERROR, "OOM: custom vol canvas");
        return;
    }
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);
    #else
    int title_y = bounds.size.h / 2 - 40;
    int val_y = bounds.size.h / 2 - 10;
    GFont title_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont val_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);

    PreferredContentSize size = preferred_content_size();
    if (size == PreferredContentSizeLarge || size == PreferredContentSizeExtraLarge) {
        title_y = bounds.size.h / 2 - 45; val_y = bounds.size.h / 2 - 15;
        title_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
        val_font = fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
    } else if (size == PreferredContentSizeSmall) {
        title_y = bounds.size.h / 2 - 35; val_y = bounds.size.h / 2 - 5;
        title_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
        val_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    }

    s_title_layer = text_layer_create(GRect(0, title_y, bounds.size.w, 30));
    s_volume_layer = text_layer_create(GRect(0, val_y, bounds.size.w, 40));

    if (!s_title_layer || !s_volume_layer) {
        app_timer_register(10, oom_pop_callback, NULL);
        return;
    }

    text_layer_set_font(s_title_layer, title_font);
    text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_title_layer, GColorClear);
    text_layer_set_text_color(s_title_layer, theme_text());
    text_layer_set_text(s_title_layer, "Adjust Volume");
    layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

    text_layer_set_font(s_volume_layer, val_font);
    text_layer_set_text_alignment(s_volume_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_volume_layer, GColorClear);
    text_layer_set_text_color(s_volume_layer, theme_text());
    layer_add_child(window_layer, text_layer_get_layer(s_volume_layer));

    update_volume_text();
    #endif
}

static void window_unload(Window *window) {
    #if defined(PBL_PLATFORM_APLITE)
    if (s_canvas_layer) { layer_destroy(s_canvas_layer); s_canvas_layer = NULL; }
    #else
    if (s_title_layer) { text_layer_destroy(s_title_layer); s_title_layer = NULL; }
    if (s_volume_layer) { text_layer_destroy(s_volume_layer); s_volume_layer = NULL; }
    #endif
}

void custom_volume_window_push(float default_volume, float default_abv) {
    s_current_volume = default_volume; s_default_abv = default_abv;

    if(!s_window) {
        s_window = window_create();
        if (!s_window) return;

        window_set_click_config_provider(s_window, click_config_provider);
        window_set_window_handlers(s_window, (WindowHandlers) {
            .load = window_load, .appear = window_appear,
            .disappear = window_disappear, .unload = window_unload,
        });
    }

    #if defined(PBL_PLATFORM_APLITE)
    window_stack_push(s_window, false);
    #else
    if (!window_stack_contains_window(s_window)) window_stack_push(s_window, true);
    #endif
}

void custom_volume_window_destroy_safe(void) {
    if (s_window && window_stack_contains_window(s_window)) {
        window_stack_remove(s_window, false);
    }
}
