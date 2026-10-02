#include <pebble.h>
#include "time_offset_menu.h"
#include "abv_window.h"
#include "portion_menu.h"
#include "custom_volume_window.h"
#include "container_menu.h"
#include "../core/storage.h"
#include "../core/touch_menu.h"

extern void app_reset_idle_timer(void);
extern Window* main_window_get_window(void);

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

static float s_volume_ml;
static float s_original_volume_ml;
static float s_abv;
static DrinkShape s_shape;

static const struct {
    const char *title;
    const char *subtitle;
    int offset_mins;
} s_offsets[] = {
    {"Now", "Just finished", 0},
    {"5 mins ago", NULL, 5},
    {"10 mins ago", NULL, 10},
    {"15 mins ago", NULL, 15},
    {"20 mins ago", NULL, 20},
    {"30 mins ago", NULL, 30},
    {"45 mins ago", NULL, 45},
    {"1 hour ago", NULL, 60},
    {"1.5 hours ago", NULL, 90},
    {"2 hours ago", NULL, 120},
};

#if !defined(PBL_PLATFORM_APLITE)
static int16_t get_dynamic_cell_height(void) {
    PreferredContentSize size = preferred_content_size();
    if (size == PreferredContentSizeLarge || size == PreferredContentSizeExtraLarge) return 56;
    if (size == PreferredContentSizeSmall) return 36;
    return 44;
}
#endif

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

static uint16_t get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return ARRAY_LENGTH(s_offsets);
}

static int16_t get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    #if defined(PBL_PLATFORM_APLITE)
    return 44;
    #else
    return get_dynamic_cell_height();
    #endif
}

static void draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    draw_dynamic_menu_cell(ctx, cell_layer, s_offsets[cell_index->row].title, s_offsets[cell_index->row].subtitle, cell_index);
}

static void select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    #if !defined(PBL_PLATFORM_APLITE)
    if (s_click_locked) return;
    s_click_locked = true;
    app_timer_register(300, unlock_click, NULL);
    #endif

    app_reset_idle_timer();
    time_t timestamp = time(NULL) - (s_offsets[cell_index->row].offset_mins * 60);

    Drink drink = {
        .timestamp = timestamp,
        .volume_ml = s_volume_ml,
        .original_volume_ml = s_original_volume_ml,
        .abv = s_abv,
        .shape = s_shape
    };

    storage_add_drink(drink);

    Window *main_win = main_window_get_window();
    if (main_win && window_stack_contains_window(main_win)) {
        while (window_stack_get_top_window() != main_win) {
            window_stack_pop(false);
        }
    } else {
        window_stack_pop_all(false);
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
    .get_num_rows = get_num_rows_callback,
    .get_cell_height = get_cell_height_callback,
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

    app_reset_idle_timer();
    if(s_menu_layer) {
        window_set_background_color(window, theme_bg());
        menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text());
        menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());
        menu_layer_reload_data(s_menu_layer);

        touch_menu_subscribe(window, s_menu_layer, s_menu_cbs, NULL);
    }
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

void time_offset_menu_push(float volume_ml, float original_volume_ml, float abv, DrinkShape shape) {
    s_volume_ml = volume_ml;
    s_original_volume_ml = original_volume_ml;
    s_abv = abv;
    s_shape = shape;

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
