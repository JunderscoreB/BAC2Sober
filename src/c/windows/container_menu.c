// ============================================================================
// File: src/windows/container_menu.c
// ============================================================================
#include <pebble.h>
#include "container_menu.h"
#include "portion_menu.h"
#include "abv_window.h"
#include "custom_volume_window.h"
#include "../core/bac_math.h"
#include "../core/storage.h"
#include "../core/touch_menu.h"
#include "../core/ui_utils.h"

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
static MenuLayer *s_menu_layer;

void container_menu_remove_from_stack(void) {
    if (s_window && window_stack_contains_window(s_window)) {
        window_stack_remove(s_window, false);
    }
}

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
static void oom_pop_callback(void *data) { window_stack_pop(false); }
#endif

typedef struct {
    const char *name;
    float volume_ml;
    float default_abv;
    DrinkShape shape;
} DrinkContainer;

static const DrinkContainer s_liquor_options_na[] = {
    {"Shot", 30.0f, 40.0f, SHAPE_SHOT},
    {"Double Shot", 60.0f, 40.0f, SHAPE_SHOT}
};
static const DrinkContainer s_beer_options_na[] = {
    {"Taster", 118.0f, 6.5f, SHAPE_TASTER},
    {"Standard Can", 355.0f, 5.0f, SHAPE_CAN},
    {"Bottle", 355.0f, 7.0f, SHAPE_BOTTLE},
    {"Tallboy Can", 473.0f, 6.5f, SHAPE_TALLBOY},
    {"US Pint", 473.0f, 5.0f, SHAPE_PINT},
    {"Growler", 1892.7f, 6.5f, SHAPE_GROWLER}
};
static const DrinkContainer s_wine_options_na[] = {
    {"Glass", 148.0f, 12.5f, SHAPE_WINE_GLASS},
    {"Half Bottle", 375.0f, 12.5f, SHAPE_WINE_BOTTLE},
    {"Full Bottle", 750.0f, 12.5f, SHAPE_WINE_BOTTLE}
};

#if !defined(PBL_PLATFORM_APLITE)
static const DrinkContainer s_liquor_options_uk[] = {
    {"Single Shot", 25.0f, 40.0f, SHAPE_SHOT}, {"Double Shot", 50.0f, 40.0f, SHAPE_SHOT}
};
static const DrinkContainer s_beer_options_uk[] = {
    {"1/3 Pint", 189.0f, 5.0f, SHAPE_TASTER}, {"Half Pint", 284.0f, 5.0f, SHAPE_PINT},
    {"Bottle", 330.0f, 5.0f, SHAPE_BOTTLE}, {"Tallboy Can", 440.0f, 5.0f, SHAPE_TALLBOY},
    {"UK Pint", 568.0f, 5.0f, SHAPE_PINT}
};
static const DrinkContainer s_wine_options_uk[] = {
    {"Small Glass", 125.0f, 12.5f, SHAPE_WINE_GLASS}, {"Medium Glass", 175.0f, 12.5f, SHAPE_WINE_GLASS},
    {"Large Glass", 250.0f, 12.5f, SHAPE_WINE_GLASS}, {"Half Bottle", 375.0f, 12.5f, SHAPE_WINE_BOTTLE},
    {"Full Bottle", 750.0f, 12.5f, SHAPE_WINE_BOTTLE}
};
static const DrinkContainer s_liquor_options_eu[] = {
    {"Shot (2cl)", 20.0f, 40.0f, SHAPE_SHOT}, {"Double (4cl)", 40.0f, 40.0f, SHAPE_SHOT}
};
static const DrinkContainer s_beer_options_eu[] = {
    {"Galopin", 250.0f, 5.0f, SHAPE_TASTER}, {"Standard Can", 330.0f, 5.0f, SHAPE_CAN},
    {"Standard Bottle", 330.0f, 5.0f, SHAPE_BOTTLE}, {"Half-Litre", 500.0f, 5.0f, SHAPE_PINT},
    {"Litre (Maß)", 1000.0f, 5.0f, SHAPE_GROWLER}
};
static const DrinkContainer s_wine_options_eu[] = {
    {"Small Glass", 100.0f, 12.5f, SHAPE_WINE_GLASS}, {"Medium Glass", 150.0f, 12.5f, SHAPE_WINE_GLASS},
    {"Large Glass", 250.0f, 12.5f, SHAPE_WINE_GLASS}, {"Half Bottle", 375.0f, 12.5f, SHAPE_WINE_BOTTLE},
    {"Full Bottle", 750.0f, 12.5f, SHAPE_WINE_BOTTLE}
};
static const DrinkContainer s_liquor_options_au[] = {
    {"Nip", 30.0f, 40.0f, SHAPE_SHOT}, {"Double", 60.0f, 40.0f, SHAPE_SHOT}
};
static const DrinkContainer s_beer_options_au[] = {
    {"Middy / Pot", 285.0f, 5.0f, SHAPE_PINT}, {"Stubby", 375.0f, 5.0f, SHAPE_BOTTLE},
    {"Schooner", 425.0f, 5.0f, SHAPE_PINT}, {"AU Pint", 570.0f, 5.0f, SHAPE_PINT}
};
static const DrinkContainer s_wine_options_au[] = {
    {"Small Glass", 150.0f, 12.5f, SHAPE_WINE_GLASS}, {"Large Glass", 250.0f, 12.5f, SHAPE_WINE_GLASS},
    {"Half Bottle", 375.0f, 12.5f, SHAPE_WINE_BOTTLE}, {"Full Bottle", 750.0f, 12.5f, SHAPE_WINE_BOTTLE}
};
#endif

static const DrinkContainer* get_liquor_array(int* out_size) {
    #if !defined(PBL_PLATFORM_APLITE)
    AppSettings *settings = storage_get_settings();
    if (settings->region == REGION_UK) { *out_size = ARRAY_LENGTH(s_liquor_options_uk); return s_liquor_options_uk; }
    if (settings->region == REGION_EU) { *out_size = ARRAY_LENGTH(s_liquor_options_eu); return s_liquor_options_eu; }
    if (settings->region == REGION_AU) { *out_size = ARRAY_LENGTH(s_liquor_options_au); return s_liquor_options_au; }
    #endif
    *out_size = ARRAY_LENGTH(s_liquor_options_na); return s_liquor_options_na;
}

static const DrinkContainer* get_beer_array(int* out_size) {
    #if !defined(PBL_PLATFORM_APLITE)
    AppSettings *settings = storage_get_settings();
    if (settings->region == REGION_UK) { *out_size = ARRAY_LENGTH(s_beer_options_uk); return s_beer_options_uk; }
    if (settings->region == REGION_EU) { *out_size = ARRAY_LENGTH(s_beer_options_eu); return s_beer_options_eu; }
    if (settings->region == REGION_AU) { *out_size = ARRAY_LENGTH(s_beer_options_au); return s_beer_options_au; }
    #endif
    *out_size = ARRAY_LENGTH(s_beer_options_na); return s_beer_options_na;
}

static const DrinkContainer* get_wine_array(int* out_size) {
    #if !defined(PBL_PLATFORM_APLITE)
    AppSettings *settings = storage_get_settings();
    if (settings->region == REGION_UK) { *out_size = ARRAY_LENGTH(s_wine_options_uk); return s_wine_options_uk; }
    if (settings->region == REGION_EU) { *out_size = ARRAY_LENGTH(s_wine_options_eu); return s_wine_options_eu; }
    if (settings->region == REGION_AU) { *out_size = ARRAY_LENGTH(s_wine_options_au); return s_wine_options_au; }
    #endif
    *out_size = ARRAY_LENGTH(s_wine_options_na); return s_wine_options_na;
}

static uint16_t get_num_sections_callback(MenuLayer *menu_layer, void *data) { return 4; }
static int16_t get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) { return ui_get_dynamic_cell_height(); }
static int16_t get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) { return UI_HEADER_HEIGHT; }

static uint16_t get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    int size = 0; AppSettings *settings = storage_get_settings();
    if (section_index == 0) return (settings->last_custom_volume_ml > 0.0f) ? 2 : 1;
    else if (section_index == 1) { get_liquor_array(&size); return size; }
    else if (section_index == 2) { get_beer_array(&size); return size; }
    else if (section_index == 3) { get_wine_array(&size); return size; }
    return 0;
}

static void draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
    if (section_index == 0) menu_cell_basic_header_draw(ctx, cell_layer, "Custom");
    else if (section_index == 1) menu_cell_basic_header_draw(ctx, cell_layer, "Liquor");
    else if (section_index == 2) menu_cell_basic_header_draw(ctx, cell_layer, "Beer");
    else if (section_index == 3) menu_cell_basic_header_draw(ctx, cell_layer, "Wine");
}

static void draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    AppSettings *settings = storage_get_settings();
    bool is_selected = false; int marquee = 0;
    #if !defined(PBL_PLATFORM_APLITE)
    is_selected = (s_selected_index.section == cell_index->section && s_selected_index.row == cell_index->row);
    marquee = s_marquee_offset;
    #endif

    if (cell_index->section == 0) {
        if (settings->last_custom_volume_ml > 0.0f) {
            if (cell_index->row == 0) {
                char subtitle[48];
                int vol_ml = (int)(settings->last_custom_volume_ml + 0.5f);
                int oz_tenths = (vol_ml * 10000 + 14786) / 29573;
                int abv_tenths = (int)(settings->last_custom_abv * 10.0f + 0.5f);

                if (settings->use_metric_volume) snprintf(subtitle, sizeof(subtitle), "%d ml | %d.%d%%", vol_ml, abv_tenths / 10, abv_tenths % 10);
                else snprintf(subtitle, sizeof(subtitle), "%d.%d oz | %d.%d%%", oz_tenths / 10, oz_tenths % 10, abv_tenths / 10, abv_tenths % 10);
                ui_draw_dynamic_menu_cell(ctx, cell_layer, "Repeat Last", subtitle, is_selected, marquee); return;
            } else {
                ui_draw_dynamic_menu_cell(ctx, cell_layer, "New Custom Size", "Dial in exact volume", is_selected, marquee); return;
            }
        } else {
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Custom Size", "Dial in exact volume", is_selected, marquee); return;
        }
    }

    char subtitle[48]; const DrinkContainer *container; int discard_size;

    if (cell_index->section == 1) container = &get_liquor_array(&discard_size)[cell_index->row];
    else if (cell_index->section == 2) container = &get_beer_array(&discard_size)[cell_index->row];
    else container = &get_wine_array(&discard_size)[cell_index->row];

    int vol_ml = (int)(container->volume_ml + 0.5f);
    int oz_tenths = (vol_ml * 10000 + 14786) / 29573;

    if (settings->use_metric_volume) snprintf(subtitle, sizeof(subtitle), "%d ml (%d.%d oz)", vol_ml, oz_tenths / 10, oz_tenths % 10);
    else snprintf(subtitle, sizeof(subtitle), "%d.%d oz (%d ml)", oz_tenths / 10, oz_tenths % 10, vol_ml);

    ui_draw_dynamic_menu_cell(ctx, cell_layer, container->name, subtitle, is_selected, marquee);
}

static void select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    #if !defined(PBL_PLATFORM_APLITE)
    if (s_click_locked) return;
    s_click_locked = true; app_timer_register(300, unlock_click, NULL);
    #endif

    app_reset_idle_timer(); AppSettings *settings = storage_get_settings();

    if (cell_index->section == 0) {
        if (settings->last_custom_volume_ml > 0.0f && cell_index->row == 0) {
            #if defined(PBL_PLATFORM_APLITE)
            float v = settings->last_custom_volume_ml;
            float abv = settings->last_custom_abv;
            bool portions = settings->enable_portions;
            if (s_window && window_stack_contains_window(s_window)) window_stack_remove(s_window, false);
            if (portions) portion_menu_push(v, v, abv, SHAPE_CUSTOM, -1);
            else abv_window_push(v, v, abv, SHAPE_CUSTOM);
            #else
            g_wizard_vol = settings->last_custom_volume_ml; g_wizard_orig_vol = settings->last_custom_volume_ml;
            g_wizard_abv = settings->last_custom_abv; g_wizard_shape = SHAPE_CUSTOM;
            g_wizard_next_step = settings->enable_portions ? 1 : 2;
            wizard_advance();
            #endif
        } else {
            #if defined(PBL_PLATFORM_APLITE)
            if (s_window && window_stack_contains_window(s_window)) window_stack_remove(s_window, false);
            custom_volume_window_push(355.0f, 5.0f);
            #else
            g_wizard_next_step = 4;
            wizard_advance();
            #endif
        }
    } else {
        const DrinkContainer *selected; int discard_size;
        if (cell_index->section == 1) selected = &get_liquor_array(&discard_size)[cell_index->row];
        else if (cell_index->section == 2) selected = &get_beer_array(&discard_size)[cell_index->row];
        else selected = &get_wine_array(&discard_size)[cell_index->row];

        #if defined(PBL_PLATFORM_APLITE)
        float v = selected->volume_ml;
        float abv = selected->default_abv;
        DrinkShape shape = selected->shape;
        bool portions = settings->enable_portions;
        if (s_window && window_stack_contains_window(s_window)) window_stack_remove(s_window, false);
        if (portions) portion_menu_push(v, v, abv, shape, -1);
        else abv_window_push(v, v, abv, shape);
        #else
        g_wizard_vol = selected->volume_ml; g_wizard_orig_vol = selected->volume_ml;
        g_wizard_abv = selected->default_abv; g_wizard_shape = selected->shape;
        g_wizard_next_step = settings->enable_portions ? 1 : 2;
        wizard_advance();
        #endif
    }
}

static void selection_changed_callback(struct MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index, void *callback_context) {
    #if !defined(PBL_PLATFORM_APLITE)
    s_selected_index = new_index; s_marquee_offset = 0;
    #endif
    app_reset_idle_timer();
}

static MenuLayerCallbacks s_menu_cbs = {
    .get_num_sections = get_num_sections_callback, .get_num_rows = get_num_rows_callback,
    .get_cell_height = get_cell_height_callback, .get_header_height = get_header_height_callback,
    .draw_header = draw_header_callback, .draw_row = draw_row_callback,
    .select_click = select_callback, .selection_changed = selection_changed_callback,
};

static void window_appear(Window *window) {
    #if !defined(PBL_PLATFORM_APLITE)
    s_click_locked = false;
    s_marquee_offset = 0;
    s_marquee_timer = app_timer_register(100, marquee_timer_callback, NULL);
    #endif

    if(s_menu_layer) {
        window_set_background_color(window, theme_bg()); menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text());
        menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());
        menu_layer_reload_data(s_menu_layer); touch_menu_subscribe(window, s_menu_layer, s_menu_cbs, NULL);
    }
    app_reset_idle_timer();
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

    menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text()); menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());
    menu_layer_set_callbacks(s_menu_layer, NULL, s_menu_cbs); menu_layer_set_click_config_onto_window(s_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void window_unload(Window *window) {
    if (s_menu_layer) { menu_layer_destroy(s_menu_layer); s_menu_layer = NULL; }
}

void container_menu_push(void) {
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
