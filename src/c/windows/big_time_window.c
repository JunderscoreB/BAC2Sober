#include <pebble.h>
#include "big_time_window.h"
#include "../core/touch_menu.h"
#include "../core/storage.h"

static Window *s_window;
static Layer *s_canvas_layer;
static time_t s_target_time = 0;
static const char* s_target_label = "Target Time";
static GColor s_time_color;
static char s_time_buffer[16];
static char s_target_buffer[16];

#if defined(PBL_TOUCH)
static bool s_show_gesture_warning = false;
static AppTimer *s_gesture_timer = NULL;
static void gesture_timer_callback(void *data) {
    s_gesture_timer = NULL; s_show_gesture_warning = false;
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}
#endif

Window* big_time_window_get_window(void) { return s_window; }

void big_time_window_update(time_t target_time, const char* target_label, GColor time_color) {
    s_target_time = target_time; if (target_label) s_target_label = target_label; s_time_color = time_color;
    if (!s_window || !window_stack_contains_window(s_window)) return;

    window_set_background_color(s_window, theme_bg());
    clock_copy_time_string(s_time_buffer, sizeof(s_time_buffer));

    if (s_target_time > 0) {
        struct tm *target_tm = localtime(&s_target_time);
        if (clock_is_24h_style()) strftime(s_target_buffer, sizeof(s_target_buffer), "%H:%M", target_tm);
        else strftime(s_target_buffer, sizeof(s_target_buffer), "%I:%M %p", target_tm);
    } else {
        snprintf(s_target_buffer, sizeof(s_target_buffer), "--:--");
        s_target_label = "Target Not Set / Reached";
    }
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

static void back_click_handler(ClickRecognizerRef recognizer, void *context) { window_stack_pop(true); }
static void click_config_provider(void *context) { window_single_click_subscribe(BUTTON_ID_BACK, back_click_handler); }

#ifdef PBL_TOUCH
static int16_t s_touch_start_x = 0; static int16_t s_touch_start_y = 0;
static void touch_handler(const TouchEvent *event, void *context) {
    if (event->type == TouchEvent_Touchdown) {
        s_touch_start_x = event->x; s_touch_start_y = event->y;
    } else if (event->type == TouchEvent_Liftoff) {
        int16_t dx = event->x - s_touch_start_x; int16_t dy = event->y - s_touch_start_y;
        if (abs(dx) > 40 && abs(dx) > abs(dy)) {
            AppSettings *settings = storage_get_settings();
            bool is_back = settings->right_handed_mode ? (dx > 40) : (dx < -40);
            if (is_back) {
                s_show_gesture_warning = true;
                if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
                if (s_gesture_timer) app_timer_cancel(s_gesture_timer);
                s_gesture_timer = app_timer_register(2000, gesture_timer_callback, NULL);
            }
        }
    }
}
#endif

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer); int offset_y = PBL_IF_ROUND_ELSE(15, 5);
    GFont header_font, time_font, target_font, warn_font; int y1, y2, y3, y4, y5;

    #ifndef PBL_PLATFORM_APLITE
    PreferredContentSize size = preferred_content_size();
    if (size == PreferredContentSizeLarge || size == PreferredContentSizeExtraLarge) {
        header_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD); time_font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
        target_font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK); warn_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
        y1 = -5; y2 = 18; y3 = 62; y4 = 85; y5 = 125;
    } else if (size == PreferredContentSizeSmall) {
        header_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD); time_font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
        target_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD); warn_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
        y1 = 5; y2 = 20; y3 = 60; y4 = 75; y5 = 110;
    } else {
        #endif
        header_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD); time_font = fonts_get_system_font(PBL_IF_COLOR_ELSE(FONT_KEY_BITHAM_42_BOLD, FONT_KEY_BITHAM_42_LIGHT));
        target_font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK); warn_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
        y1 = 0; y2 = 18; y3 = 65; y4 = 85; y5 = 125;
        #ifndef PBL_PLATFORM_APLITE
    }
    #endif

    y1 += offset_y; y2 += offset_y; y3 += offset_y; y4 += offset_y; y5 += offset_y;

    graphics_context_set_text_color(ctx, theme_text());
    graphics_draw_text(ctx, "Current Time", header_font, GRect(0, y1, bounds.size.w, 30), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, s_target_label, header_font, GRect(0, y3, bounds.size.w, 30), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    graphics_context_set_text_color(ctx, s_time_color);
    graphics_draw_text(ctx, s_time_buffer, time_font, GRect(0, y2, bounds.size.w, 46), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, s_target_buffer, target_font, GRect(0, y4, bounds.size.w, 46), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    #if defined(PBL_TOUCH)
    if (s_show_gesture_warning) {
        graphics_context_set_text_color(ctx, theme_text());
        graphics_draw_text(ctx, "Use back button", warn_font, GRect(0, y5, bounds.size.w, 30), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
    #else
    (void)warn_font; (void)y5;
    #endif
}

static void oom_pop_callback(void *data) { window_stack_pop(false); }

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window); GRect bounds = layer_get_bounds(window_layer);
    window_set_background_color(window, theme_bg());

    s_canvas_layer = layer_create(bounds);
    if (!s_canvas_layer) {
        app_timer_register(10, oom_pop_callback, NULL);
        return;
    }

    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);
    big_time_window_update(s_target_time, s_target_label, s_time_color);
}

static void window_appear(Window *window) {
    #ifdef PBL_TOUCH
    if (touch_service_is_enabled()) touch_service_subscribe(touch_handler, NULL);
    #endif
}

static void window_disappear(Window *window) {
    #ifdef PBL_TOUCH
    if (touch_service_is_enabled()) touch_service_unsubscribe();
    if (s_gesture_timer) { app_timer_cancel(s_gesture_timer); s_gesture_timer = NULL; }
    s_show_gesture_warning = false;
    #endif
}

static void destroy_window_cb(void *data) { window_destroy((Window*)data); }

static void window_unload(Window *window) {
    if (s_canvas_layer) { layer_destroy(s_canvas_layer); s_canvas_layer = NULL; }
    if (window == s_window) {
        s_window = NULL;
        app_timer_register(50, destroy_window_cb, window);
    }
}

void big_time_window_push(time_t target_time, const char* target_label, GColor time_color) {
    s_target_time = target_time; if (target_label) s_target_label = target_label; s_time_color = time_color;

    if (!s_window) {
        s_window = window_create();
        if (!s_window) return;
        window_set_click_config_provider(s_window, click_config_provider);
        window_set_window_handlers(s_window, (WindowHandlers) {
            .load = window_load, .appear = window_appear,
            .disappear = window_disappear, .unload = window_unload
        });
    }
    if (!window_stack_contains_window(s_window)) window_stack_push(s_window, true);
}
