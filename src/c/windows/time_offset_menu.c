#include <pebble.h>
#include "time_offset_menu.h"
#include "../core/bac_math.h"
#include "../core/storage.h"

static Window *s_window;
static MenuLayer *s_menu_layer;

static float s_selected_volume_ml;
static float s_selected_abv_decimal;

#define NUM_TIME_OPTIONS 7
static const int s_offsets_mins[] = {0, 15, 30, 45, 60, 90, 120};
static const char* s_offset_names[] = {
    "Just Now",
    "15 Mins Ago",
    "30 Mins Ago",
    "45 Mins Ago",
    "1 Hour Ago",
    "1.5 Hours Ago",
    "2 Hours Ago"
};

static uint16_t get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return NUM_TIME_OPTIONS;
}

static void draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    menu_cell_basic_draw(ctx, cell_layer, s_offset_names[cell_index->row], NULL, NULL);
}

static void select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    uint32_t offset_seconds = s_offsets_mins[cell_index->row] * 60;

    Drink d = {
        .timestamp = time(NULL) - offset_seconds,
        .volume_ml = s_selected_volume_ml,
        .abv = s_selected_abv_decimal
    };
    storage_add_drink(d);

    // Unwind the navigation stack to reveal the main dashboard again
    window_stack_pop(false); // Pop Time Offset Menu
    window_stack_pop(false); // Pop ABV Window
    window_stack_pop(false); // Pop Portion Menu
    window_stack_pop(true);  // Pop Container Menu (Animate the transition back to Main)
}

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    s_menu_layer = menu_layer_create(bounds);
    menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
        .get_num_rows = get_num_rows_callback,
        .draw_row = draw_row_callback,
        .select_click = select_callback,
    });
    menu_layer_set_click_config_onto_window(s_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void window_unload(Window *window) {
    menu_layer_destroy(s_menu_layer);
    window_destroy(s_window);
    s_window = NULL;
}

void time_offset_menu_push(float volume_ml, float abv_decimal) {
    s_selected_volume_ml = volume_ml;
    s_selected_abv_decimal = abv_decimal;

    if(!s_window) {
        s_window = window_create();
        window_set_window_handlers(s_window, (WindowHandlers) {
            .load = window_load,
            .unload = window_unload,
        });
    }
    window_stack_push(s_window, true);
}
