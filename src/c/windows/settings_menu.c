#include <pebble.h>
#include "settings_menu.h"
#include "weight_window.h"
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

// Region option drops entirely on Aplite to save array logic memory
typedef enum {
    ROW_WEIGHT = 0,
    ROW_SEX,
    ROW_WEIGHT_UNIT,
    ROW_VOL_UNIT,
    ROW_SPLIT_DRINK,
    #if defined(PBL_TOUCH)
    ROW_BACK_GESTURE,
    #endif
    ROW_TARGET_BAC,
    #if !defined(PBL_PLATFORM_APLITE)
    ROW_REGION,
    #endif
    ROW_IDLE_TIMEOUT,
    ROW_THEME,
    ROW_CLEAR_ALL,
    NUM_SETTINGS_ROWS
} SettingsRow;

static Window *s_clear_window;
static TextLayer *s_clear_prompt_layer;
static TextLayer *s_clear_yes_layer;
static TextLayer *s_clear_no_layer;

static void clear_yes_click_handler(ClickRecognizerRef recognizer, void *context) {
    storage_clear_drinks();
    window_stack_pop(false);
    window_stack_pop(true);
}

static void clear_no_click_handler(ClickRecognizerRef recognizer, void *context) {
    window_stack_pop(true);
}

static void clear_click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, clear_yes_click_handler);
    window_single_click_subscribe(BUTTON_ID_BACK, clear_no_click_handler);
}

static void clear_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    window_set_background_color(window, theme_bg());

    s_clear_prompt_layer = text_layer_create(GRect(10, bounds.size.h / 2 - 40, bounds.size.w - 20, 60));
    text_layer_set_font(s_clear_prompt_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_clear_prompt_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_clear_prompt_layer, GColorClear);
    text_layer_set_text_color(s_clear_prompt_layer, theme_text());
    text_layer_set_text(s_clear_prompt_layer, "Clear all drinks?");
    layer_add_child(window_layer, text_layer_get_layer(s_clear_prompt_layer));

    s_clear_no_layer = text_layer_create(GRect(5, bounds.size.h / 2 - 12, 50, 30));
    text_layer_set_font(s_clear_no_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_clear_no_layer, GTextAlignmentLeft);
    text_layer_set_background_color(s_clear_no_layer, GColorClear);
    text_layer_set_text_color(s_clear_no_layer, theme_text());
    text_layer_set_text(s_clear_no_layer, "← No");
    layer_add_child(window_layer, text_layer_get_layer(s_clear_no_layer));

    s_clear_yes_layer = text_layer_create(GRect(bounds.size.w - 55, bounds.size.h / 2 - 12, 50, 30));
    text_layer_set_font(s_clear_yes_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_clear_yes_layer, GTextAlignmentRight);
    text_layer_set_background_color(s_clear_yes_layer, GColorClear);
    text_layer_set_text_color(s_clear_yes_layer, theme_text());
    text_layer_set_text(s_clear_yes_layer, "Yes →");
    layer_add_child(window_layer, text_layer_get_layer(s_clear_yes_layer));
}

static void clear_window_unload(Window *window) {
    text_layer_destroy(s_clear_prompt_layer);
    text_layer_destroy(s_clear_yes_layer);
    text_layer_destroy(s_clear_no_layer);
    window_destroy(s_clear_window);
    s_clear_window = NULL;
}

static void push_clear_prompt(void) {
    if(!s_clear_window) {
        s_clear_window = window_create();
        window_set_click_config_provider(s_clear_window, clear_click_config_provider);
        window_set_window_handlers(s_clear_window, (WindowHandlers) {
            .load = clear_window_load,
            .unload = clear_window_unload,
        });
    }
    window_stack_push(s_clear_window, true);
}

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
    return NUM_SETTINGS_ROWS;
}

static int16_t get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    #if defined(PBL_PLATFORM_APLITE)
    return 44;
    #else
    return get_dynamic_cell_height();
    #endif
}

static void draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    AppSettings *settings = storage_get_settings();
    char subtitle[32];

    switch (cell_index->row) {
        case ROW_WEIGHT:
            if (settings->use_metric_weight) snprintf(subtitle, sizeof(subtitle), "%d kg", (int)settings->weight);
            else snprintf(subtitle, sizeof(subtitle), "%d lbs", (int)settings->weight);
            draw_dynamic_menu_cell(ctx, cell_layer, "Weight", subtitle, cell_index);
        break;

        case ROW_SEX:
            snprintf(subtitle, sizeof(subtitle), settings->gender_constant > 0.6f ? "Male" : "Female");
            draw_dynamic_menu_cell(ctx, cell_layer, "Biological Sex", subtitle, cell_index);
            break;

        case ROW_WEIGHT_UNIT:
            snprintf(subtitle, sizeof(subtitle), settings->use_metric_weight ? "Kilograms (kg)" : "Pounds (lbs)");
            draw_dynamic_menu_cell(ctx, cell_layer, "Weight Unit", subtitle, cell_index);
            break;

        case ROW_VOL_UNIT:
            snprintf(subtitle, sizeof(subtitle), settings->use_metric_volume ? "Metric (ml)" : "Imperial (oz)");
            draw_dynamic_menu_cell(ctx, cell_layer, "Volume Unit", subtitle, cell_index);
            break;

        case ROW_SPLIT_DRINK:
            snprintf(subtitle, sizeof(subtitle), settings->enable_portions ? "Enabled" : "Disabled");
            draw_dynamic_menu_cell(ctx, cell_layer, "Split Drinks", subtitle, cell_index);
            break;

            #if defined(PBL_TOUCH)
        case ROW_BACK_GESTURE:
            snprintf(subtitle, sizeof(subtitle), settings->right_handed_mode ? "Right (Swipe →)" : "Left (Swipe ←)");
            draw_dynamic_menu_cell(ctx, cell_layer, "Back Gesture", subtitle, cell_index);
            break;
            #endif

        case ROW_TARGET_BAC:
            if (settings->target_bac < 0.0f) {
                snprintf(subtitle, sizeof(subtitle), "Disable timeline reporting");
            } else {
                int target_tenths = (int)(settings->target_bac * 100.0f + 0.5f);
                snprintf(subtitle, sizeof(subtitle), "%d.%02d%%", target_tenths / 100, target_tenths % 100);
            }
            draw_dynamic_menu_cell(ctx, cell_layer, "Target BAC (for timeline)", subtitle, cell_index);
            break;

            #if !defined(PBL_PLATFORM_APLITE)
        case ROW_REGION:
            if (settings->region == REGION_UK) snprintf(subtitle, sizeof(subtitle), "United Kingdom");
            else if (settings->region == REGION_EU) snprintf(subtitle, sizeof(subtitle), "Europe");
            else if (settings->region == REGION_AU) snprintf(subtitle, sizeof(subtitle), "Australia");
            else snprintf(subtitle, sizeof(subtitle), "North America");
            draw_dynamic_menu_cell(ctx, cell_layer, "Region", subtitle, cell_index);
        break;
        #endif

        case ROW_IDLE_TIMEOUT:
            if (settings->idle_timeout_mins == 0) {
                snprintf(subtitle, sizeof(subtitle), "Infinite");
            } else if (settings->idle_timeout_mins == 1) {
                snprintf(subtitle, sizeof(subtitle), "1 Minute");
            } else {
                snprintf(subtitle, sizeof(subtitle), "%d Minutes", settings->idle_timeout_mins);
            }
            draw_dynamic_menu_cell(ctx, cell_layer, "Idle Timeout", subtitle, cell_index);
            break;

        case ROW_THEME:
            if (settings->theme_mode == THEME_MODE_LIGHT) snprintf(subtitle, sizeof(subtitle), "Light");
            else if (settings->theme_mode == THEME_MODE_DARK) snprintf(subtitle, sizeof(subtitle), "Dark");
            else snprintf(subtitle, sizeof(subtitle), "Auto (6pm - 6am)");
            draw_dynamic_menu_cell(ctx, cell_layer, "Theme", subtitle, cell_index);
        break;

        case ROW_CLEAR_ALL:
            draw_dynamic_menu_cell(ctx, cell_layer, "Clear All Drinks", "Resets BAC to 0.00", cell_index);
            break;
    }
}

static void select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    #if !defined(PBL_PLATFORM_APLITE)
    if (s_click_locked) return;
    s_click_locked = true;
    app_timer_register(300, unlock_click, NULL);
    #endif

    AppSettings *settings = storage_get_settings();
    app_reset_idle_timer();

    switch (cell_index->row) {
        case ROW_WEIGHT:
            weight_window_push();
            break;

        case ROW_SEX:
            settings->gender_constant = (settings->gender_constant > 0.6f) ? 0.55f : 0.68f;
            storage_save_settings();
            menu_layer_reload_data(menu_layer);
            break;

        case ROW_WEIGHT_UNIT:
            if (settings->use_metric_weight) {
                settings->weight = settings->weight * 2.20462f;
                settings->use_metric_weight = false;
            } else {
                settings->weight = settings->weight / 2.20462f;
                settings->use_metric_weight = true;
            }
            storage_save_settings();
            menu_layer_reload_data(menu_layer);
            break;

        case ROW_VOL_UNIT:
            settings->use_metric_volume = !settings->use_metric_volume;
            storage_save_settings();
            menu_layer_reload_data(menu_layer);
            break;

        case ROW_SPLIT_DRINK:
            settings->enable_portions = !settings->enable_portions;
            storage_save_settings();
            menu_layer_reload_data(menu_layer);
            break;

            #if defined(PBL_TOUCH)
        case ROW_BACK_GESTURE:
            settings->right_handed_mode = !settings->right_handed_mode;
            storage_save_settings();
            menu_layer_reload_data(menu_layer);
            break;
            #endif

        case ROW_TARGET_BAC: {
            static const float targets[] = {-1.0f, 0.00f, 0.02f, 0.04f, 0.05f, 0.06f, 0.07f, 0.08f};
            int curr = 0;
            for(int i=0; i<8; i++) { if (settings->target_bac >= targets[i] - 0.001f) curr = i; }
            curr = (curr + 1) % 8;
            settings->target_bac = targets[curr];
            storage_save_settings();
            menu_layer_reload_data(menu_layer);
            break;
        }

        #if !defined(PBL_PLATFORM_APLITE)
        case ROW_REGION:
            settings->region = (settings->region + 1) % 4;
            storage_save_settings();
            menu_layer_reload_data(menu_layer);
            break;
            #endif

        case ROW_IDLE_TIMEOUT: {
            static const uint8_t timeouts[] = {0, 1, 2, 5, 10, 15};
            int curr = 0;
            for(int i=0; i<6; i++) { if (settings->idle_timeout_mins == timeouts[i]) curr = i; }
            curr = (curr + 1) % 6;
            settings->idle_timeout_mins = timeouts[curr];
            storage_save_settings();
            menu_layer_reload_data(menu_layer);
            break;
        }

        case ROW_THEME:
            if (settings->theme_mode == THEME_MODE_LIGHT) settings->theme_mode = THEME_MODE_DARK;
            else if (settings->theme_mode == THEME_MODE_DARK) settings->theme_mode = THEME_MODE_AUTO;
            else settings->theme_mode = THEME_MODE_LIGHT;

            storage_save_settings();
        window_set_background_color(s_window, theme_bg());
        menu_layer_set_normal_colors(menu_layer, theme_bg(), theme_text());
        menu_layer_set_highlight_colors(menu_layer, theme_highlight_bg(), theme_highlight_text());
        menu_layer_reload_data(menu_layer);
        break;

        case ROW_CLEAR_ALL:
            push_clear_prompt();
            break;
    }
}

static void selection_changed_callback(struct MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index, void *callback_context) {
    #if !defined(PBL_PLATFORM_APLITE)
    s_selected_index = new_index;
    s_marquee_offset = 0;
    #endif
    app_reset_idle_timer();
}

static MenuLayerCallbacks s_settings_cbs = {
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

    if(s_menu_layer) {
        window_set_background_color(window, theme_bg());
        menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text());
        menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());
        menu_layer_reload_data(s_menu_layer);

        touch_menu_subscribe(window, s_menu_layer, s_settings_cbs, NULL);
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
