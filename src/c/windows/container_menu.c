#include <pebble.h>
#include "container_menu.h"
#include "portion_menu.h"
#include "abv_window.h"
#include "custom_volume_window.h"
#include "../core/bac_math.h"
#include "../core/storage.h"
#include "../core/touch_menu.h"

typedef struct {
    const char *name;
    float volume_ml;
    float default_abv;
    DrinkShape shape;
} DrinkContainer;

static Window *s_window;
static MenuLayer *s_menu_layer;

#define NUM_LIQUOR_OPTIONS 2
static const DrinkContainer s_liquor_options[] = {
    {"Shot", 30.0f, 40.0f, SHAPE_SHOT},
    {"Double Shot", 60.0f, 40.0f, SHAPE_SHOT}
};

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

static uint16_t get_num_sections_callback(MenuLayer *menu_layer, void *data) { 
    return 4; 
}

static uint16_t get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    if (section_index == 0) return NUM_LIQUOR_OPTIONS;
    if (section_index == 1) return NUM_BEER_OPTIONS;
    if (section_index == 2) return NUM_WINE_OPTIONS;
    if (section_index == 3) return 1; 
    return 0;
}

static int16_t get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    return UI_ROW_HEIGHT;
}

static int16_t get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return UI_HEADER_HEIGHT;
}

static void draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
    if (section_index == 0) menu_cell_basic_header_draw(ctx, cell_layer, "Liquor");
    else if (section_index == 1) menu_cell_basic_header_draw(ctx, cell_layer, "Beer");
    else if (section_index == 2) menu_cell_basic_header_draw(ctx, cell_layer, "Wine");
    else if (section_index == 3) menu_cell_basic_header_draw(ctx, cell_layer, "Custom");
}

static void draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    if (cell_index->section == 3) {
        menu_cell_basic_draw(ctx, cell_layer, "Custom Size", "Dial in exact volume", NULL);
        return;
    }

    char subtitle[32];
    const DrinkContainer *container;
    
    if (cell_index->section == 0) container = &s_liquor_options[cell_index->row];
    else if (cell_index->section == 1) container = &s_beer_options[cell_index->row];
    else container = &s_wine_options[cell_index->row];

    snprintf(subtitle, sizeof(subtitle), "%d ml (%d oz)", (int)container->volume_ml, (int)(container->volume_ml / 29.5735f));
    menu_cell_basic_draw(ctx, cell_layer, container->name, subtitle, NULL);
}

static void select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    if (cell_index->section == 3) {
        custom_volume_window_push(355.0f, 5.0f);
        return;
    }

    const DrinkContainer *selected;
    if (cell_index->section == 0) selected = &s_liquor_options[cell_index->row];
    else if (cell_index->section == 1) selected = &s_beer_options[cell_index->row];
    else selected = &s_wine_options[cell_index->row];

    AppSettings *settings = storage_get_settings();
    if (settings->enable_portions) {
        // Pass original volume, current volume, default abv, shape, and -1 (not an edit)
        portion_menu_push(selected->volume_ml, selected->volume_ml, selected->default_abv, selected->shape, -1);
    } else {
        // Skip portion wizard, pass volume for both fields, abv, and shape
        abv_window_push(selected->volume_ml, selected->volume_ml, selected->default_abv, selected->shape);
    }
}

static MenuLayerCallbacks s_menu_cbs = {
    .get_num_sections = get_num_sections_callback,
    .get_num_rows = get_num_rows_callback,
    .get_cell_height = get_cell_height_callback,
    .get_header_height = get_header_height_callback,
    .draw_header = draw_header_callback,
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

void container_menu_push(void) {
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

void container_menu_destroy_safe(void) {
    if (s_window && window_stack_contains_window(s_window)) {
        window_stack_remove(s_window, false);
    }
}