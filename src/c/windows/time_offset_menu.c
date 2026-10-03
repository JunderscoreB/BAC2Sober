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

static Window *s_window;
static MenuLayer *s_menu_layer;

#if !defined(PBL_PLATFORM_APLITE)
static MenuIndex s_selected_index = {0, 0};
static AppTimer *s_marquee_timer = NULL;
static int s_marquee_offset = 0;
static bool s_click_locked = false;

static void unlock_click(void *data) { s_click_locked = false; }
static void marquee_timer_callback(void *data) {
    s_marquee_offset += 3;
    if (s_menu_layer) layer_mark_dirty(menu_layer_get_layer(s_menu_layer));
    s_marquee_timer = app_timer_register(100, marquee_timer_callback, NULL);
}
#endif

static float s_volume_ml;
static float s_original_volume_ml;
static float s_abv;
static DrinkShape s_shape;

static const int16_t s_offsets_mins[] = { 0, 5, 10, 15, 20, 30, 45, 60, 90, 120 };

static uint16_t get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) { return ARRAY_LENGTH(s_offsets_mins); }
static int16_t get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) { return ui_get_dynamic_cell_height(); }

static void draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    bool is_selected = false; int marquee = 0;
    #if !defined(PBL_PLATFORM_APLITE)
    is_selected = (s_selected_index.section == cell_index->section && s_selected_index.row == cell_index->row);
    marquee = s_marquee_offset;
    #endif

    int mins = s_offsets_mins[cell_index->row];
    char title[16]; const char *sub = NULL;

    if (mins == 0) { strcpy(title, "Now"); sub = "Just finished"; }
    else if (mins < 60) snprintf(title, sizeof(title), "%d mins ago", mins);
    else if (mins == 60) snprintf(title, sizeof(title), "1 hour ago");
    else if (mins == 90) snprintf(title, sizeof(title), "1.5 hours ago");
    else snprintf(title, sizeof(title), "2 hours ago");

    ui_draw_dynamic_menu_cell(ctx, cell_layer, title, sub, is_selected, marquee);
}

static void select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    #if !defined(PBL_PLATFORM_APLITE)
    if (s_click_locked) return;
    s_click_locked = true; app_timer_register(300, unlock_click, NULL);
    #endif

    app_reset_idle_timer();
    time_t timestamp = time(NULL) - (s_offsets_mins[cell_index->row] * 60);

    Drink drink = {
        .timestamp = timestamp, .volume_ml = s_volume_ml,
        .original_volume_ml = s_original_volume_ml, .abv = s_abv, .shape = s_shape
    };

    storage_add_drink(drink);
    window_stack_pop(true); // Complete sequence: Pop back to Dashboard safely!
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
    s_click_locked = false; s_marquee_offset = 0;
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

static void oom_pop_callback(void *data) { window_stack_pop(false); }

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window); GRect bounds = layer_get_bounds(window_layer);
    window_set_background_color(window, theme_bg());

    s_menu_layer = menu_layer_create(bounds);
    if (!s_menu_layer) {
        app_timer_register(10, oom_pop_callback, NULL);
        return;
    }

    menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text());
    menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());
    menu_layer_set_callbacks(s_menu_layer, NULL, s_menu_cbs);
    menu_layer_set_click_config_onto_window(s_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void destroy_window_cb(void *data) { window_destroy((Window*)data); }

static void window_unload(Window *window) {
    if (s_menu_layer) { menu_layer_destroy(s_menu_layer); s_menu_layer = NULL; }
    if (window == s_window) {
        s_window = NULL;
        app_timer_register(50, destroy_window_cb, window);
    }
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
    if (!window_stack_contains_window(s_window)) window_stack_push(s_window, true);
}
