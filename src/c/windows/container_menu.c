#include <pebble.h>
#include "container_menu.h"
#include "portion_menu.h"
#include "custom_volume_window.h"
#include "../core/bac_math.h"
#include "../core/storage.h"

static Window *s_window;
static MenuLayer *s_menu_layer;

#define NUM_BEER_OPTIONS 6
static const DrinkContainer s_beer_options[] = {
    {"Taster", 118.0f, 6.5f, SHAPE_TASTER},
    {"Standard Can", 355.0f, 5.0f, SHAPE_CAN},
    {"Bottle", 355.0f, 7.0f, SHAPE_BOTTLE},
    {"Tallboy Can", 473.0f, 6.5f, SHAPE_TALLBOY},
    {"European Pint", 500.0f, 5.0f, SHAPE_PINT},
    {"Growler", 946.0f, 6.5f, SHAPE_GROWLER}
};

#define NUM_WINE_OPTIONS 3
static const DrinkContainer s_wine_options[] = {
    {"Glass", 148.0f, 12.5f, SHAPE_WINE_GLASS},
    {"Half Bottle", 375.0f, 12.5f, SHAPE_WINE_BOTTLE},
    {"Full Bottle", 750.0f, 12.5f, SHAPE_WINE_BOTTLE}
};

static uint16_t get_num_sections_callback(MenuLayer *menu_layer, void *data) { return 3; }

static uint16_t get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    if (section_index == 0) return NUM_BEER_OPTIONS;
    if (section_index == 1) return NUM_WINE_OPTIONS;
    if (section_index == 2) return 1;
    return 0;
}

static int16_t get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
    if (section_index == 0) menu_cell_basic_header_draw(ctx, cell_layer, "Beer");
    else if (section_index == 1) menu_cell_basic_header_draw(ctx, cell_layer, "Wine");
    else if (section_index == 2) menu_cell_basic_header_draw(ctx, cell_layer, "Custom");
}

static void draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    if (cell_index->section == 2) {
        menu_cell_basic_draw(ctx, cell_layer, "Custom Size", "Dial in exact volume", NULL);
        return;
    }

    char subtitle[32];
    const DrinkContainer *container = (cell_index->section == 0) ?
    &s_beer_options[cell_index->row] : &s_wine_options[cell_index->row];

    snprintf(subtitle, sizeof(subtitle), "%d ml (%d oz)", (int)container->volume_ml, (int)(container->volume_ml / 29.5735f));
    menu_cell_basic_draw(ctx, cell_layer, container->name, subtitle, NULL);
}

static void select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    if (cell_index->section == 2) {
        custom_volume_window_push(355.0f, 5.0f);
        return;
    }

    const DrinkContainer *selected = (cell_index->section == 0) ?
    &s_beer_options[cell_index->row] : &s_wine_options[cell_index->row];

    portion_menu_push(selected->volume_ml, selected->default_abv, selected->shape);
}

static void window_appear(Window *window) {
    if(s_menu_layer) {
        window_set_background_color(window, theme_bg());
        menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text());
        menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());
        menu_layer_reload_data(s_menu_layer);
    }
}

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    s_menu_layer = menu_layer_create(bounds);

    window_set_background_color(window, theme_bg());
    menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text());
    menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());

    menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
        .get_num_sections = get_num_sections_callback,
        .get_num_rows = get_num_rows_callback,
        .get_header_height = get_header_height_callback,
        .draw_header = draw_header_callback,
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

void container_menu_push(void) {
    if(!s_window) {
        s_window = window_create();
        window_set_window_handlers(s_window, (WindowHandlers) {
            .load = window_load,
            .appear = window_appear,
            .unload = window_unload,
        });
    }
    window_stack_push(s_window, true);
}
