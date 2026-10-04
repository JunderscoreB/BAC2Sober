// ============================================================================
// File: src/windows/time_offset_menu.c
// ============================================================================
#include <pebble.h>
#include "time_offset_menu.h"
#include "abv_window.h"
#include "portion_menu.h"
#include "custom_volume_window.h"
#include "container_menu.h"
#include "../core/storage.h"
#include "../core/touch_menu.h"
#include "../core/ui_utils.h"

extern void app_reset_idle_timer(void);

#if defined(PBL_PLATFORM_APLITE)
extern Window* main_window_get_window(void);
#else
extern void container_menu_remove_from_stack(void);
extern void portion_menu_remove_from_stack(void);
extern void abv_window_remove_from_stack(void);
extern void custom_volume_window_remove_from_stack(void);
#endif

static Window *s_window;
static MenuLayer *s_menu_layer;

#if !defined(PBL_PLATFORM_APLITE)
static MenuIndex s_selected_index = {0, 0};
static AppTimer *s_marquee_timer = NULL;
static int s_marquee_offset = 0;
static void marquee_timer_callback(void *data) {
    s_marquee_offset += 3;
    if (s_menu_layer) layer_mark_dirty(menu_layer_get_layer(s_menu_layer));
    s_marquee_timer = app_timer_register(100, marquee_timer_callback, NULL);
}

static bool s_click_locked = false;
static void unlock_click(void *data) { s_click_locked = false; }
#endif

static float s_volume_ml;
static float s_original_volume_ml;
static float s_abv;
static DrinkShape s_shape;

#if defined(PBL_PLATFORM_APLITE)
static const struct {
    const char *title;
    const char *subtitle;
    int offset_mins;
} s_offsets[] = {
    {"Now", "Just finished", 0}, {"5 mins ago", NULL, 5},
    {"10 mins ago", NULL, 10}, {"15 mins ago", NULL, 15},
    {"20 mins ago", NULL, 20}, {"30 mins ago", NULL, 30},
    {"45 mins ago", NULL, 45}, {"1 hour ago", NULL, 60},
    {"1.5 hours ago", NULL, 90}, {"2 hours ago", NULL, 120},
};
#else
static const int16_t s_offsets_mins[] = { 0, 5, 10, 15, 20, 30, 45, 60, 90, 120 };
#endif

// --- LOG FULL CONFIRMATION PROMPT WINDOW ---
static Window *s_overflow_window = NULL;
static Drink s_overflow_drink;

#if defined(PBL_PLATFORM_APLITE)
static Layer *s_overflow_canvas = NULL;
#else
static TextLayer *s_overflow_prompt_layer = NULL;
static TextLayer *s_overflow_sub_layer = NULL;
static TextLayer *s_overflow_yes_layer = NULL;
static TextLayer *s_overflow_no_layer = NULL;
#endif

#if !defined(PBL_PLATFORM_APLITE)
static void oom_pop_callback(void *data) { window_stack_pop(false); }

static void pop_wizard_stack(void) {
    container_menu_remove_from_stack();
    portion_menu_remove_from_stack();
    abv_window_remove_from_stack();
    custom_volume_window_remove_from_stack();
}
#endif

static void overflow_yes_click_handler(ClickRecognizerRef recognizer, void *context) {
    #if defined(PBL_PLATFORM_APLITE)
    storage_drop_oldest_drink();
    storage_add_drink(s_overflow_drink);

    Window *main_win = main_window_get_window();
    if (main_win && window_stack_contains_window(main_win)) {
        while (window_stack_get_top_window() != main_win) {
            window_stack_pop(false);
        }
    } else {
        window_stack_pop_all(false);
    }
    #else
    storage_add_drink_overwrite(s_overflow_drink);
    window_stack_pop(false);
    pop_wizard_stack();
    window_stack_pop(true);
    #endif
}

static void overflow_no_click_handler(ClickRecognizerRef recognizer, void *context) {
    #if defined(PBL_PLATFORM_APLITE)
    window_stack_pop(false);
    #else
    window_stack_pop(true);
    #endif
}

static void overflow_click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, overflow_yes_click_handler);
    window_single_click_subscribe(BUTTON_ID_BACK, overflow_no_click_handler);
}

#if defined(PBL_TOUCH)
static void overflow_touch_handler(const TouchEvent *event, void *context) {
    app_reset_idle_timer();
    if (event->type == TouchEvent_Liftoff) {
        if (!s_overflow_window) return;
        Layer *root = window_get_root_layer(s_overflow_window);
        if (!root) return;
        GRect b = layer_get_bounds(root);
        if (event->x > b.size.w / 2) overflow_yes_click_handler(NULL, NULL);
        else overflow_no_click_handler(NULL, NULL);
    }
}
#endif

#if defined(PBL_PLATFORM_APLITE)
static void overflow_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    graphics_context_set_text_color(ctx, theme_text());

    GFont font_24 = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont font_18 = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

    graphics_draw_text(ctx, "Drink Log Full\nDrop oldest?", font_24,
                       GRect(5, 10, bounds.size.w - 10, 60),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    int button_y = (bounds.size.h / 2) + 15;

    graphics_draw_text(ctx, "<- No", font_18,
                       GRect(5, button_y, 50, 30),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    graphics_draw_text(ctx, "Yes ->", font_18,
                       GRect(bounds.size.w - 55, button_y, 50, 30),
                       GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
}
#endif

static void overflow_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);
    window_set_background_color(window, theme_bg());

    #if defined(PBL_PLATFORM_APLITE)
    s_overflow_canvas = layer_create(bounds);
    if (!s_overflow_canvas) return;
    layer_set_update_proc(s_overflow_canvas, overflow_update_proc);
    layer_add_child(window_layer, s_overflow_canvas);
    #else
    GFont prompt_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont sub_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    GFont btn_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    int p_y = bounds.size.h / 2 - 45;
    int s_y = bounds.size.h / 2 - 15;
    int n_y = bounds.size.h - 30;

    PreferredContentSize size = preferred_content_size();
    if (size == PreferredContentSizeLarge || size == PreferredContentSizeExtraLarge) {
        prompt_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
        sub_font = fonts_get_system_font(FONT_KEY_GOTHIC_24);
        btn_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
        p_y -= 5; n_y -= 10;
    } else if (size == PreferredContentSizeSmall) {
        prompt_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
        sub_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
        btn_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
        p_y += 10; s_y += 5; n_y += 5;
    }

    s_overflow_prompt_layer = text_layer_create(GRect(5, p_y, bounds.size.w - 10, 35));
    s_overflow_sub_layer = text_layer_create(GRect(5, s_y, bounds.size.w - 10, 35));
    s_overflow_no_layer = text_layer_create(GRect(5, n_y, 70, 35));
    s_overflow_yes_layer = text_layer_create(GRect(bounds.size.w - 75, n_y, 70, 35));

    if (!s_overflow_prompt_layer || !s_overflow_sub_layer || !s_overflow_no_layer || !s_overflow_yes_layer) {
        window_stack_pop(false);
        return;
    }

    text_layer_set_font(s_overflow_prompt_layer, prompt_font);
    text_layer_set_text_alignment(s_overflow_prompt_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_overflow_prompt_layer, GColorClear);
    text_layer_set_text_color(s_overflow_prompt_layer, theme_text());
    text_layer_set_text(s_overflow_prompt_layer, "Drink Log Full");
    layer_add_child(window_layer, text_layer_get_layer(s_overflow_prompt_layer));

    text_layer_set_font(s_overflow_sub_layer, sub_font);
    text_layer_set_text_alignment(s_overflow_sub_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_overflow_sub_layer, GColorClear);
    text_layer_set_text_color(s_overflow_sub_layer, theme_text());
    text_layer_set_text(s_overflow_sub_layer, "Drop oldest drink?");
    layer_add_child(window_layer, text_layer_get_layer(s_overflow_sub_layer));

    text_layer_set_font(s_overflow_no_layer, btn_font);
    text_layer_set_text_alignment(s_overflow_no_layer, GTextAlignmentLeft);
    text_layer_set_background_color(s_overflow_no_layer, GColorClear);
    text_layer_set_text_color(s_overflow_no_layer, theme_text());
    text_layer_set_text(s_overflow_no_layer, "<- No");
    layer_add_child(window_layer, text_layer_get_layer(s_overflow_no_layer));

    text_layer_set_font(s_overflow_yes_layer, btn_font);
    text_layer_set_text_alignment(s_overflow_yes_layer, GTextAlignmentRight);
    text_layer_set_background_color(s_overflow_yes_layer, GColorClear);
    text_layer_set_text_color(s_overflow_yes_layer, theme_text());
    text_layer_set_text(s_overflow_yes_layer, "Yes ->");
    layer_add_child(window_layer, text_layer_get_layer(s_overflow_yes_layer));
    #endif
}

static void overflow_window_appear(Window *window) {
    app_reset_idle_timer();
    #if defined(PBL_TOUCH)
    if (touch_service_is_enabled()) touch_service_subscribe(overflow_touch_handler, NULL);
    #endif
}

static void overflow_window_disappear(Window *window) {
    #if defined(PBL_TOUCH)
    if (touch_service_is_enabled()) touch_service_unsubscribe();
    #endif
}

static void overflow_window_unload(Window *window) {
    #if defined(PBL_PLATFORM_APLITE)
    if (s_overflow_canvas) { layer_destroy(s_overflow_canvas); s_overflow_canvas = NULL; }
    #else
    if (s_overflow_prompt_layer) { text_layer_destroy(s_overflow_prompt_layer); s_overflow_prompt_layer = NULL; }
    if (s_overflow_sub_layer) { text_layer_destroy(s_overflow_sub_layer); s_overflow_sub_layer = NULL; }
    if (s_overflow_no_layer) { text_layer_destroy(s_overflow_no_layer); s_overflow_no_layer = NULL; }
    if (s_overflow_yes_layer) { text_layer_destroy(s_overflow_yes_layer); s_overflow_yes_layer = NULL; }
    #endif
}

static void push_overflow_prompt(void) {
    if (!s_overflow_window) {
        s_overflow_window = window_create();
        if (!s_overflow_window) return;
        window_set_click_config_provider(s_overflow_window, overflow_click_config_provider);
        window_set_window_handlers(s_overflow_window, (WindowHandlers) {
            .load = overflow_window_load, .appear = overflow_window_appear,
            .disappear = overflow_window_disappear, .unload = overflow_window_unload,
        });
    }

    #if defined(PBL_PLATFORM_APLITE)
    window_stack_push(s_overflow_window, false);
    #else
    if (!window_stack_contains_window(s_overflow_window)) {
        window_stack_push(s_overflow_window, true);
    }
    #endif
}
// -------------------------------------------

static uint16_t get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    #if defined(PBL_PLATFORM_APLITE)
    return ARRAY_LENGTH(s_offsets);
    #else
    return ARRAY_LENGTH(s_offsets_mins);
    #endif
}

static int16_t get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) { return ui_get_dynamic_cell_height(); }

static void draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    bool is_selected = false; int marquee = 0;
    #if !defined(PBL_PLATFORM_APLITE)
    is_selected = (s_selected_index.section == cell_index->section && s_selected_index.row == cell_index->row);
    marquee = s_marquee_offset;
    #endif

    #if defined(PBL_PLATFORM_APLITE)
    ui_draw_dynamic_menu_cell(ctx, cell_layer, s_offsets[cell_index->row].title, s_offsets[cell_index->row].subtitle, is_selected, marquee);
    #else
    int mins = s_offsets_mins[cell_index->row];
    char title[16]; const char *sub = NULL;

    if (mins == 0) { strcpy(title, "Now"); sub = "Just finished"; }
    else if (mins < 60) snprintf(title, sizeof(title), "%d mins ago", mins);
    else if (mins == 60) snprintf(title, sizeof(title), "1 hour ago");
    else if (mins == 90) snprintf(title, sizeof(title), "1.5 hours ago");
    else snprintf(title, sizeof(title), "2 hours ago");

    ui_draw_dynamic_menu_cell(ctx, cell_layer, title, sub, is_selected, marquee);
    #endif
}

static void select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    #if !defined(PBL_PLATFORM_APLITE)
    if (s_click_locked) return;
    s_click_locked = true; app_timer_register(300, unlock_click, NULL);
    #endif

    app_reset_idle_timer();

    #if defined(PBL_PLATFORM_APLITE)
    time_t timestamp = time(NULL) - (s_offsets[cell_index->row].offset_mins * 60);
    #else
    time_t timestamp = time(NULL) - (s_offsets_mins[cell_index->row] * 60);
    #endif

    Drink drink = {
        .timestamp = timestamp, .volume_ml = s_volume_ml,
        .original_volume_ml = s_original_volume_ml, .abv = s_abv, .shape = s_shape
    };

    if (storage_add_drink(drink)) {
        #if defined(PBL_PLATFORM_APLITE)
        Window *main_win = main_window_get_window();
        if (main_win && window_stack_contains_window(main_win)) {
            while (window_stack_get_top_window() != main_win) {
                window_stack_pop(false);
            }
        } else {
            window_stack_pop_all(false);
        }
        #else
        pop_wizard_stack();
        window_stack_pop(true);
        #endif
    } else {
        s_overflow_drink = drink;
        push_overflow_prompt();
    }
}

static void selection_changed_callback(struct MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index, void *callback_context) {
    #if !defined(PBL_PLATFORM_APLITE)
    s_selected_index = new_index; s_marquee_offset = 0;
    #endif
    app_reset_idle_timer();
}

static MenuLayerCallbacks s_menu_cbs = {
    .get_num_rows = get_num_rows_callback, .get_cell_height = get_cell_height_callback,
    .draw_row = draw_row_callback, .select_click = select_callback,
    .selection_changed = selection_changed_callback,
};

static void window_appear(Window *window) {
    #if !defined(PBL_PLATFORM_APLITE)
    s_click_locked = false;
    s_marquee_offset = 0;
    s_marquee_timer = app_timer_register(100, marquee_timer_callback, NULL);
    #endif

    app_reset_idle_timer();
    if(s_menu_layer) {
        window_set_background_color(window, theme_bg());
        menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text());
        menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());
        menu_layer_reload_data(s_menu_layer); touch_menu_subscribe(window, s_menu_layer, s_menu_cbs, NULL);
    }
}

static void window_disappear(Window *window) {
    #if !defined(PBL_PLATFORM_APLITE)
    if (s_marquee_timer) { app_timer_cancel(s_marquee_timer); s_marquee_timer = NULL; }
    #endif
    touch_menu_unsubscribe();
}

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window); GRect bounds = layer_get_bounds(window_layer);
    window_set_background_color(window, theme_bg());

    s_menu_layer = menu_layer_create(bounds);
    if (!s_menu_layer) {
        #if !defined(PBL_PLATFORM_APLITE)
        app_timer_register(10, oom_pop_callback, NULL);
        #endif
        return;
    }

    menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text());
    menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());
    menu_layer_set_callbacks(s_menu_layer, NULL, s_menu_cbs);
    menu_layer_set_click_config_onto_window(s_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void window_unload(Window *window) {
    if (s_menu_layer) { menu_layer_destroy(s_menu_layer); s_menu_layer = NULL; }
}

void time_offset_menu_push(float volume_ml, float original_volume_ml, float abv, DrinkShape shape) {
    s_volume_ml = volume_ml; s_original_volume_ml = original_volume_ml;
    s_abv = abv; s_shape = shape;

    if(!s_window) {
        s_window = window_create();
        if (!s_window) return;

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
