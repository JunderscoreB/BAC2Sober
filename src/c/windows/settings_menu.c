#include <pebble.h>
#include "settings_menu.h"
#include "weight_window.h"
#include "../core/storage.h"
#include "../core/touch_menu.h"

static Window *s_window;
static MenuLayer *s_menu_layer;

static uint16_t get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return 6; 
}

static int16_t get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    return UI_ROW_HEIGHT;
}

static void draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    AppSettings *settings = storage_get_settings();
    char subtitle[32];

    if (cell_index->row == 0) {
        if (settings->use_metric_weight) snprintf(subtitle, sizeof(subtitle), "%d kg", (int)settings->weight);
        else snprintf(subtitle, sizeof(subtitle), "%d lbs", (int)settings->weight);
        menu_cell_basic_draw(ctx, cell_layer, "Weight", subtitle, NULL);
    } else if (cell_index->row == 1) {
        snprintf(subtitle, sizeof(subtitle), settings->gender_constant > 0.6f ? "Male" : "Female");
        menu_cell_basic_draw(ctx, cell_layer, "Biological Sex", subtitle, NULL);
    } else if (cell_index->row == 2) {
        snprintf(subtitle, sizeof(subtitle), settings->use_metric_weight ? "Kilograms (kg)" : "Pounds (lbs)");
        menu_cell_basic_draw(ctx, cell_layer, "Weight Unit", subtitle, NULL);
    } else if (cell_index->row == 3) {
        snprintf(subtitle, sizeof(subtitle), settings->use_metric_volume ? "Metric (ml)" : "Imperial (oz)");
        menu_cell_basic_draw(ctx, cell_layer, "Volume Unit", subtitle, NULL);
    } else if (cell_index->row == 4) {
        if (settings->theme_mode == THEME_MODE_LIGHT) snprintf(subtitle, sizeof(subtitle), "Light");
        else if (settings->theme_mode == THEME_MODE_DARK) snprintf(subtitle, sizeof(subtitle), "Dark");
        else snprintf(subtitle, sizeof(subtitle), "Auto (6pm - 6am)");
        menu_cell_basic_draw(ctx, cell_layer, "Theme", subtitle, NULL);
    } else if (cell_index->row == 5) {
        menu_cell_basic_draw(ctx, cell_layer, "Clear All Drinks", "Resets BAC to 0.00", NULL);
    }
}

static void select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    AppSettings *settings = storage_get_settings();

    if (cell_index->row == 0) {
        weight_window_push();
    } else if (cell_index->row == 1) {
        settings->gender_constant = (settings->gender_constant > 0.6f) ? 0.55f : 0.68f;
        storage_save_settings();
        menu_layer_reload_data(menu_layer);
    } else if (cell_index->row == 2) {
        if (settings->use_metric_weight) {
            settings->weight = settings->weight * 2.20462f; 
            settings->use_metric_weight = false;
        } else {
            settings->weight = settings->weight / 2.20462f; 
            settings->use_metric_weight = true;
        }
        storage_save_settings();
        menu_layer_reload_data(menu_layer);
    } else if (cell_index->row == 3) {
        settings->use_metric_volume = !settings->use_metric_volume;
        storage_save_settings();
        menu_layer_reload_data(menu_layer);
    } else if (cell_index->row == 4) {
        if (settings->theme_mode == THEME_MODE_LIGHT) settings->theme_mode = THEME_MODE_DARK;
        else if (settings->theme_mode == THEME_MODE_DARK) settings->theme_mode = THEME_MODE_AUTO;
        else settings->theme_mode = THEME_MODE_LIGHT;
        
        storage_save_settings();
        window_set_background_color(s_window, theme_bg());
        menu_layer_set_normal_colors(menu_layer, theme_bg(), theme_text());
        menu_layer_set_highlight_colors(menu_layer, theme_highlight_bg(), theme_highlight_text());
        menu_layer_reload_data(menu_layer);
    } else if (cell_index->row == 5) {
        storage_clear_drinks();
        window_stack_pop(true);
    }
}

static MenuLayerCallbacks s_settings_cbs = {
    .get_num_rows = get_num_rows_callback,
    .get_cell_height = get_cell_height_callback,
    .draw_row = draw_row_callback,
    .select_click = select_callback,
};

static void window_appear(Window *window) {
    if(s_menu_layer) {
        window_set_background_color(window, theme_bg());
        menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text());
        menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());
        menu_layer_reload_data(s_menu_layer);
        
        touch_menu_subscribe(window, s_menu_layer, s_settings_cbs, NULL);
    }
}

static void window_disappear(Window *window) {
    touch_menu_unsubscribe();
}

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    window_set_background_color(window, theme_bg());

    s_menu_layer = menu_layer_create(bounds);
    menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text());
    menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());
    menu_layer_set_callbacks(s_menu_layer, NULL, s_settings_cbs);
    menu_layer_set_click_config_onto_window(s_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void window_unload(Window *window) {
    menu_layer_destroy(s_menu_layer);
    window_destroy(s_window);
    s_window = NULL;
}

void settings_menu_push(void) {
    if(!s_window) {
        s_window = window_create();
        window_set_window_handlers(s_window, (WindowHandlers) {
            .load = window_load,
            .appear = window_appear,
            .disappear = window_disappear,
            .unload = window_unload,
        });
    }
    window_stack_push(s_window, true);
}