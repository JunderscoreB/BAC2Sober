#include <pebble.h>
#include "container_menu.h"
#include "portion_menu.h"
#include "abv_window.h"
#include "custom_volume_window.h"
#include "../core/bac_math.h"
#include "../core/storage.h"
#include "../core/touch_menu.h"

extern void app_reset_idle_timer(void);

static Window *s_window;
static MenuLayer *s_menu_layer;

#if !defined(PBL_PLATFORM_APLITE)
static MenuIndex s_selected_index = {0, 0};
static AppTimer *s_marquee_timer = NULL;
static int s_marquee_offset = 0;
static bool s_click_locked = false;

static void unlock_click(void *data) {
    s_click_locked = false;
}

static void marquee_timer_callback(void *data) {
    s_marquee_offset += 3;
    if (s_menu_layer) {
        layer_mark_dirty(menu_layer_get_layer(s_menu_layer));
    }
    s_marquee_timer = app_timer_register(50, marquee_timer_callback, NULL);
}
#endif

// To save space, we define arrays using char[16] to enforce .rodata storage rather than pointers in .data
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
// Exclude these entirely on Aplite to save >1KB of footprint
static const DrinkContainer s_liquor_options_uk[] = {
    {"Single Shot", 25.0f, 40.0f, SHAPE_SHOT},
    {"Double Shot", 50.0f, 40.0f, SHAPE_SHOT}
};
static const DrinkContainer s_beer_options_uk[] = {
    {"1/3 Pint", 189.0f, 5.0f, SHAPE_TASTER},
    {"Half Pint", 284.0f, 5.0f, SHAPE_PINT},
    {"Bottle", 330.0f, 5.0f, SHAPE_BOTTLE},
    {"Tallboy Can", 440.0f, 5.0f, SHAPE_TALLBOY},
    {"UK Pint", 568.0f, 5.0f, SHAPE_PINT}
};
static const DrinkContainer s_wine_options_uk[] = {
    {"Small Glass", 125.0f, 12.5f, SHAPE_WINE_GLASS},
    {"Medium Glass", 175.0f, 12.5f, SHAPE_WINE_GLASS},
    {"Large Glass", 250.0f, 12.5f, SHAPE_WINE_GLASS},
    {"Half Bottle", 375.0f, 12.5f, SHAPE_WINE_BOTTLE},
    {"Full Bottle", 750.0f, 12.5f, SHAPE_WINE_BOTTLE}
};

static const DrinkContainer s_liquor_options_eu[] = {
    {"Shot (2cl)", 20.0f, 40.0f, SHAPE_SHOT},
    {"Double (4cl)", 40.0f, 40.0f, SHAPE_SHOT}
};
static const DrinkContainer s_beer_options_eu[] = {
    {"Galopin", 250.0f, 5.0f, SHAPE_TASTER},
    {"Standard Can", 330.0f, 5.0f, SHAPE_CAN},
    {"Standard Bottle", 330.0f, 5.0f, SHAPE_BOTTLE},
    {"Half-Litre", 500.0f, 5.0f, SHAPE_PINT},
    {"Litre (Maß)", 1000.0f, 5.0f, SHAPE_GROWLER}
};
static const DrinkContainer s_wine_options_eu[] = {
    {"Small Glass", 100.0f, 12.5f, SHAPE_WINE_GLASS},
    {"Medium Glass", 150.0f, 12.5f, SHAPE_WINE_GLASS},
    {"Large Glass", 250.0f, 12.5f, SHAPE_WINE_GLASS},
    {"Half Bottle", 375.0f, 12.5f, SHAPE_WINE_BOTTLE},
    {"Full Bottle", 750.0f, 12.5f, SHAPE_WINE_BOTTLE}
};

static const DrinkContainer s_liquor_options_au[] = {
    {"Nip", 30.0f, 40.0f, SHAPE_SHOT},
    {"Double", 60.0f, 40.0f, SHAPE_SHOT}
};
static const DrinkContainer s_beer_options_au[] = {
    {"Middy / Pot", 285.0f, 5.0f, SHAPE_PINT},
    {"Stubby", 375.0f, 5.0f, SHAPE_BOTTLE},
    {"Schooner", 425.0f, 5.0f, SHAPE_PINT},
    {"AU Pint", 570.0f, 5.0f, SHAPE_PINT}
};
static const DrinkContainer s_wine_options_au[] = {
    {"Small Glass", 150.0f, 12.5f, SHAPE_WINE_GLASS},
    {"Large Glass", 250.0f, 12.5f, SHAPE_WINE_GLASS},
    {"Half Bottle", 375.0f, 12.5f, SHAPE_WINE_BOTTLE},
    {"Full Bottle", 750.0f, 12.5f, SHAPE_WINE_BOTTLE}
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

static void draw_dynamic_menu_cell(GContext *ctx, const Layer *cell_layer, const char *title, const char *subtitle, MenuIndex *cell_index) {
    GRect bounds = layer_get_bounds(cell_layer);

    #if defined(PBL_PLATFORM_APLITE)
    (void)cell_index;
    GFont title_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont sub_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    int title_y = subtitle ? -4 : (bounds.size.h - 24) / 2 - 4;
    graphics_draw_text(ctx, title, title_font, GRect(5, title_y, bounds.size.w - 10, 32), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    if (subtitle) {
        graphics_draw_text(ctx, subtitle, sub_font, GRect(5, 20, bounds.size.w - 10, 26), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
    #else
    bool is_selected = (s_selected_index.section == cell_index->section && s_selected_index.row == cell_index->row);
    GFont title_font, sub_font;
    int title_y, sub_y, title_h, sub_h;

    PreferredContentSize size = preferred_content_size();
    if (size == PreferredContentSizeLarge || size == PreferredContentSizeExtraLarge) {
        title_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
        sub_font = fonts_get_system_font(FONT_KEY_GOTHIC_24);
        title_y = -2; sub_y = 26; title_h = 28; sub_h = 24;
    } else if (size == PreferredContentSizeSmall) {
        title_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
        sub_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
        title_y = -2; sub_y = 16; title_h = 18; sub_h = 14;
    } else { // Medium
        title_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
        sub_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
        title_y = -4; sub_y = 20; title_h = 24; sub_h = 18;
    }

    if (!subtitle) {
        title_y = (bounds.size.h - title_h) / 2 - 4;
    }

    int shift_x = 0;
    GSize title_size = graphics_text_layout_get_content_size(title, title_font, GRect(0, 0, 1000, title_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);

    #if defined(PBL_ROUND)
    if (bounds.size.w <= 180) { // Chalk (Stacked)
        int max_w = bounds.size.w - 10;
        if (is_selected && title_size.w > max_w) {
            int overflow = title_size.w - max_w;
            int cycle = overflow * 2 + 20;
            int tick = s_marquee_offset % cycle;
            if (tick < 10) shift_x = 0;
            else if (tick < 10 + overflow) shift_x = tick - 10;
            else if (tick < 20 + overflow) shift_x = overflow;
            else shift_x = overflow - (tick - (20 + overflow));
        }

        graphics_draw_text(ctx, title, title_font, GRect(5 - shift_x, title_y, max_w + shift_x, title_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        if (subtitle) {
            graphics_draw_text(ctx, subtitle, sub_font, GRect(5, sub_y, bounds.size.w - 10, sub_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        }
    } else { // Gabbro (Side-by-side)
        int max_w = subtitle ? (bounds.size.w - 50) : (bounds.size.w - 20);
        int title_width = title_size.w + 4;
        if (title_width > max_w) title_width = max_w;

        if (is_selected && title_size.w > max_w) {
            int overflow = title_size.w - max_w;
            int cycle = overflow * 2 + 20;
            int tick = s_marquee_offset % cycle;
            if (tick < 10) shift_x = 0;
            else if (tick < 10 + overflow) shift_x = tick - 10;
            else if (tick < 20 + overflow) shift_x = overflow;
            else shift_x = overflow - (tick - (20 + overflow));
        }

        if (subtitle) {
            int sub_w = bounds.size.w - title_width - 20;
            if (sub_w < 0) sub_w = 0;

            graphics_draw_text(ctx, title, title_font, GRect(10 - shift_x, bounds.size.h / 2 - (title_h/2) - 4, title_width + shift_x, title_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
            graphics_draw_text(ctx, subtitle, sub_font, GRect(10 + title_width, bounds.size.h / 2 - (sub_h/2) - 2, sub_w, sub_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
        } else {
            graphics_draw_text(ctx, title, title_font, GRect(10 - shift_x, bounds.size.h / 2 - (title_h/2) - 4, max_w + shift_x, title_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        }
    }
    #else
    int max_w = bounds.size.w - 10;
    if (is_selected && title_size.w > max_w) {
        int overflow = title_size.w - max_w;
        int cycle = overflow * 2 + 20;
        int tick = s_marquee_offset % cycle;
        if (tick < 10) shift_x = 0;
        else if (tick < 10 + overflow) shift_x = tick - 10;
        else if (tick < 20 + overflow) shift_x = overflow;
        else shift_x = overflow - (tick - (20 + overflow));
    }

    graphics_draw_text(ctx, title, title_font, GRect(5 - shift_x, title_y, max_w + shift_x, title_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    if (subtitle) {
        graphics_draw_text(ctx, subtitle, sub_font, GRect(5, sub_y, bounds.size.w - 10, sub_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
    #endif
    #endif
}

static uint16_t get_num_sections_callback(MenuLayer *menu_layer, void *data) {
    return 4;
}

static uint16_t get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    int size = 0;
    if (section_index == 0) {
        AppSettings *settings = storage_get_settings();
        return (settings->last_custom_volume_ml > 0.0f) ? 2 : 1;
    }
    else if (section_index == 1) { get_liquor_array(&size); return size; }
    else if (section_index == 2) { get_beer_array(&size); return size; }
    else if (section_index == 3) { get_wine_array(&size); return size; }
    return 0;
}

#if !defined(PBL_PLATFORM_APLITE)
static int16_t get_dynamic_cell_height(void) {
    PreferredContentSize size = preferred_content_size();
    if (size == PreferredContentSizeLarge || size == PreferredContentSizeExtraLarge) return 56;
    if (size == PreferredContentSizeSmall) return 36;
    return 44;
}
#endif

static int16_t get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    #if defined(PBL_PLATFORM_APLITE)
    return 44;
    #else
    return get_dynamic_cell_height();
    #endif
}

static int16_t get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return UI_HEADER_HEIGHT;
}

static void draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
    if (section_index == 0) menu_cell_basic_header_draw(ctx, cell_layer, "Custom");
    else if (section_index == 1) menu_cell_basic_header_draw(ctx, cell_layer, "Liquor");
    else if (section_index == 2) menu_cell_basic_header_draw(ctx, cell_layer, "Beer");
    else if (section_index == 3) menu_cell_basic_header_draw(ctx, cell_layer, "Wine");
}

static void draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    AppSettings *settings = storage_get_settings();

    if (cell_index->section == 0) {
        if (settings->last_custom_volume_ml > 0.0f) {
            if (cell_index->row == 0) {
                char subtitle[32];
                float oz = (settings->last_custom_volume_ml / 29.5735f) + 0.05f;
                int oz_w = (int)oz;
                int oz_d = (int)(oz * 10.0f) % 10;
                int abv_tenths = (int)(settings->last_custom_abv * 10.0f + 0.5f);

                if (settings->use_metric_volume) {
                    snprintf(subtitle, sizeof(subtitle), "%d ml | %d.%d%%", (int)settings->last_custom_volume_ml, abv_tenths / 10, abv_tenths % 10);
                } else {
                    snprintf(subtitle, sizeof(subtitle), "%d.%d oz | %d.%d%%", oz_w, oz_d, abv_tenths / 10, abv_tenths % 10);
                }
                draw_dynamic_menu_cell(ctx, cell_layer, "Repeat Last", subtitle, cell_index);
                return;
            } else {
                draw_dynamic_menu_cell(ctx, cell_layer, "New Custom Size", "Dial in exact volume", cell_index);
                return;
            }
        } else {
            draw_dynamic_menu_cell(ctx, cell_layer, "Custom Size", "Dial in exact volume", cell_index);
            return;
        }
    }

    char subtitle[32];
    const DrinkContainer *container;
    int discard_size;

    if (cell_index->section == 1) {
        const DrinkContainer *liquor_array = get_liquor_array(&discard_size);
        container = &liquor_array[cell_index->row];
    } else if (cell_index->section == 2) {
        const DrinkContainer *beer_array = get_beer_array(&discard_size);
        container = &beer_array[cell_index->row];
    } else {
        const DrinkContainer *wine_array = get_wine_array(&discard_size);
        container = &wine_array[cell_index->row];
    }

    float oz = (container->volume_ml / 29.5735f) + 0.05f;
    int oz_w = (int)oz;
    int oz_d = (int)(oz * 10.0f) % 10;

    if (settings->use_metric_volume) {
        snprintf(subtitle, sizeof(subtitle), "%d ml (%d.%d oz)", (int)container->volume_ml, oz_w, oz_d);
    } else {
        snprintf(subtitle, sizeof(subtitle), "%d.%d oz (%d ml)", oz_w, oz_d, (int)container->volume_ml);
    }

    draw_dynamic_menu_cell(ctx, cell_layer, container->name, subtitle, cell_index);
}

static void select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    #if !defined(PBL_PLATFORM_APLITE)
    if (s_click_locked) return;
    s_click_locked = true;
    app_timer_register(300, unlock_click, NULL);
    #endif

    app_reset_idle_timer();
    AppSettings *settings = storage_get_settings();

    if (cell_index->section == 0) {
        if (settings->last_custom_volume_ml > 0.0f && cell_index->row == 0) {
            if (settings->enable_portions) {
                portion_menu_push(settings->last_custom_volume_ml, settings->last_custom_volume_ml, settings->last_custom_abv, SHAPE_CUSTOM, -1);
            } else {
                abv_window_push(settings->last_custom_volume_ml, settings->last_custom_volume_ml, settings->last_custom_abv, SHAPE_CUSTOM);
            }
            return;
        } else {
            custom_volume_window_push(355.0f, 5.0f);
            return;
        }
    }

    const DrinkContainer *selected;
    int discard_size;

    if (cell_index->section == 1) {
        const DrinkContainer *liquor_array = get_liquor_array(&discard_size);
        selected = &liquor_array[cell_index->row];
    } else if (cell_index->section == 2) {
        const DrinkContainer *beer_array = get_beer_array(&discard_size);
        selected = &beer_array[cell_index->row];
    } else {
        const DrinkContainer *wine_array = get_wine_array(&discard_size);
        selected = &wine_array[cell_index->row];
    }

    if (settings->enable_portions) {
        portion_menu_push(selected->volume_ml, selected->volume_ml, selected->default_abv, selected->shape, -1);
    } else {
        abv_window_push(selected->volume_ml, selected->volume_ml, selected->default_abv, selected->shape);
    }
}

static void selection_changed_callback(struct MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index, void *callback_context) {
    #if !defined(PBL_PLATFORM_APLITE)
    s_selected_index = new_index;
    s_marquee_offset = 0;
    #endif
    app_reset_idle_timer();
}

static MenuLayerCallbacks s_menu_cbs = {
    .get_num_sections = get_num_sections_callback,
    .get_num_rows = get_num_rows_callback,
    .get_cell_height = get_cell_height_callback,
    .get_header_height = get_header_height_callback,
    .draw_header = draw_header_callback,
    .draw_row = draw_row_callback,
    .select_click = select_callback,
    .selection_changed = selection_changed_callback,
};

static void window_appear(Window *window) {
    #if !defined(PBL_PLATFORM_APLITE)
    s_click_locked = false;
    s_marquee_offset = 0;
    s_marquee_timer = app_timer_register(100, marquee_timer_callback, NULL);
    #endif

    if(s_menu_layer) {
        window_set_background_color(window, theme_bg());
        menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text());
        menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());
        menu_layer_reload_data(s_menu_layer);

        touch_menu_subscribe(window, s_menu_layer, s_menu_cbs, NULL);
    }
    app_reset_idle_timer();
}

static void window_disappear(Window *window) {
    #if !defined(PBL_PLATFORM_APLITE)
    if (s_marquee_timer) {
        app_timer_cancel(s_marquee_timer);
        s_marquee_timer = NULL;
    }
    #endif
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
