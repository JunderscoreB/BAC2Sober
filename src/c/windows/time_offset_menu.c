#include <pebble.h>
#include "time_offset_menu.h"
#include "../core/storage.h"
#include "../core/touch_menu.h"

static Window *s_window;
static MenuLayer *s_menu_layer;
static float s_current_volume_ml;
static float s_current_abv;

// External declarations so we can wipe the wizard backstack cleanly
extern void container_menu_destroy_safe(void);
extern void custom_volume_window_destroy_safe(void);
extern void portion_menu_destroy_safe(void);
extern void abv_window_destroy_safe(void);

static uint16_t get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return 6;
}

static int16_t get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    return UI_ROW_HEIGHT;
}

static void draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    switch (cell_index->row) {
        case 0: menu_cell_basic_draw(ctx, cell_layer, "Just Now", "Drink finished", NULL); break;
        case 1: menu_cell_basic_draw(ctx, cell_layer, "5 Minutes Ago", NULL, NULL); break;
        case 2: menu_cell_basic_draw(ctx, cell_layer, "10 Minutes Ago", NULL, NULL); break;
        case 3: menu_cell_basic_draw(ctx, cell_layer, "15 Minutes Ago", NULL, NULL); break;
        case 4: menu_cell_basic_draw(ctx, cell_layer, "30 Minutes Ago", NULL, NULL); break;
        case 5: menu_cell_basic_draw(ctx, cell_layer, "1 Hour Ago", NULL, NULL); break;
    }
}

static void select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    time_t timestamp = time(NULL);
    switch (cell_index->row) {
        case 1: timestamp -= 5 * 60; break;
        case 2: timestamp -= 10 * 60; break;
        case 3: timestamp -= 15 * 60; break;
        case 4: timestamp -= 30 * 60; break;
        case 5: timestamp -= 60 * 60; break;
    }

    Drink new_drink = {
        .timestamp = timestamp,
        .volume_ml = s_current_volume_ml,
        .abv = s_current_abv
    };
    storage_add_drink(new_drink);

    // Silently remove the entire wizard flow from the window stack underneath us
    container_menu_destroy_safe();
    custom_volume_window_destroy_safe();
    portion_menu_destroy_safe();
    abv_window_destroy_safe();

    // Now simply popping this window reveals the main dashboard!
    window_stack_pop(true);
}

static MenuLayerCallbacks s_menu_cbs = {
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
        
        touch_menu_subscribe(window, s_menu_layer, s_menu_cbs, NULL);
    }
}

static void window_disappear(Window *window) {
    touch_menu_unsubscribe();
}

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    s_menu_layer = menu_layer_create(bounds);
    
    window_set_background_color(window, theme_bg());
    menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text());
    menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());

    menu_layer_set_callbacks(s_menu_layer, NULL, s_menu_cbs);
    menu_layer_set_click_config_onto_window(s_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void window_unload(Window *window) {
    menu_layer_destroy(s_menu_layer);
    window_destroy(s_window);
    s_window = NULL;
}

void time_offset_menu_push(float volume_ml, float abv) {
    s_current_volume_ml = volume_ml;
    s_current_abv = abv;

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