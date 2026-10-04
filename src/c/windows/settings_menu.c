// ============================================================================
// File: src/windows/settings_menu.c
// ============================================================================
#include <pebble.h>
#include "settings_menu.h"
#include "weight_window.h"
#include "../core/storage.h"
#include "../core/touch_menu.h"
#include "../core/ui_utils.h"

extern void app_reset_idle_timer(void);

static Window *s_window;
static MenuLayer *s_menu_layer;

static bool s_clear_confirm = false;

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

typedef enum {
    ROW_WEIGHT = 0, ROW_SEX, ROW_WEIGHT_UNIT, ROW_VOL_UNIT, ROW_SPLIT_DRINK,
    #if defined(PBL_TOUCH)
    ROW_BACK_GESTURE,
    #endif
    ROW_TARGET_BAC,
    #if !defined(PBL_PLATFORM_APLITE)
    ROW_REGION,
    #endif
    ROW_IDLE_TIMEOUT, ROW_THEME, ROW_CLEAR_ALL, NUM_SETTINGS_ROWS
} SettingsRow;

static Window *s_clear_window;

#if defined(PBL_PLATFORM_APLITE)
static Layer *s_clear_canvas;

static void clear_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    graphics_context_set_text_color(ctx, theme_text());

    GFont font_24 = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont font_18 = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

    graphics_draw_text(ctx, "Clear all drinks?", font_24,
                       GRect(10, bounds.size.h / 2 - 40, bounds.size.w - 20, 60),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    graphics_draw_text(ctx, "<- No", font_18,
                       GRect(5, bounds.size.h / 2 - 12, 50, 30),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    graphics_draw_text(ctx, "Yes ->", font_18,
                       GRect(bounds.size.w - 55, bounds.size.h / 2 - 12, 50, 30),
                       GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
}
#else
static TextLayer *s_clear_prompt_layer;
static TextLayer *s_clear_yes_layer;
static TextLayer *s_clear_no_layer;
#endif

static void clear_yes_click_handler(ClickRecognizerRef recognizer, void *context) {
    storage_clear_drinks();
    window_stack_pop(false);
    #if defined(PBL_PLATFORM_APLITE)
    window_stack_pop(false);
    #else
    window_stack_pop(true);
    #endif
}

static void clear_no_click_handler(ClickRecognizerRef recognizer, void *context) {
    #if defined(PBL_PLATFORM_APLITE)
    window_stack_pop(false);
    #else
    window_stack_pop(true);
    #endif
}

static void clear_click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, clear_yes_click_handler);
    window_single_click_subscribe(BUTTON_ID_BACK, clear_no_click_handler);
}

static void clear_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window); GRect bounds = layer_get_bounds(window_layer);
    window_set_background_color(window, theme_bg());

    #if defined(PBL_PLATFORM_APLITE)
    s_clear_canvas = layer_create(bounds);
    if (!s_clear_canvas) return;
    layer_set_update_proc(s_clear_canvas, clear_update_proc);
    layer_add_child(window_layer, s_clear_canvas);
    #else
    GFont prompt_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont btn_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    int p_y = bounds.size.h / 2 - 40;
    int p_h = 60;
    int n_y = bounds.size.h / 2 - 12;

    PreferredContentSize size = preferred_content_size();
    if (size == PreferredContentSizeLarge || size == PreferredContentSizeExtraLarge) {
        prompt_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
        btn_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
        p_y -= 10;
        p_h = 70;
        n_y += 5;
    } else if (size == PreferredContentSizeSmall) {
        prompt_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
        btn_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
        p_y += 10;
        p_h = 40;
        n_y -= 5;
    }

    s_clear_prompt_layer = text_layer_create(GRect(5, p_y, bounds.size.w - 10, p_h));
    s_clear_no_layer = text_layer_create(GRect(5, n_y, 75, 30));
    s_clear_yes_layer = text_layer_create(GRect(bounds.size.w - 80, n_y, 75, 30));

    if (!s_clear_prompt_layer || !s_clear_no_layer || !s_clear_yes_layer) {
        app_timer_register(10, oom_pop_callback, NULL);
        return;
    }

    text_layer_set_font(s_clear_prompt_layer, prompt_font);
    text_layer_set_text_alignment(s_clear_prompt_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_clear_prompt_layer, GColorClear);
    text_layer_set_text_color(s_clear_prompt_layer, theme_text());
    text_layer_set_text(s_clear_prompt_layer, "Clear all drinks?");
    layer_add_child(window_layer, text_layer_get_layer(s_clear_prompt_layer));

    text_layer_set_font(s_clear_no_layer, btn_font);
    text_layer_set_text_alignment(s_clear_no_layer, GTextAlignmentLeft);
    text_layer_set_background_color(s_clear_no_layer, GColorClear);
    text_layer_set_text_color(s_clear_no_layer, theme_text());
    text_layer_set_text(s_clear_no_layer, "<- No");
    layer_add_child(window_layer, text_layer_get_layer(s_clear_no_layer));

    text_layer_set_font(s_clear_yes_layer, btn_font);
    text_layer_set_text_alignment(s_clear_yes_layer, GTextAlignmentRight);
    text_layer_set_background_color(s_clear_yes_layer, GColorClear);
    text_layer_set_text_color(s_clear_yes_layer, theme_text());
    text_layer_set_text(s_clear_yes_layer, "Yes ->");
    layer_add_child(window_layer, text_layer_get_layer(s_clear_yes_layer));
    #endif
}

static void clear_window_unload(Window *window) {
    #if defined(PBL_PLATFORM_APLITE)
    if (s_clear_canvas) { layer_destroy(s_clear_canvas); s_clear_canvas = NULL; }
    #else
    if (s_clear_prompt_layer) { text_layer_destroy(s_clear_prompt_layer); s_clear_prompt_layer = NULL; }
    if (s_clear_yes_layer) { text_layer_destroy(s_clear_yes_layer); s_clear_yes_layer = NULL; }
    if (s_clear_no_layer) { text_layer_destroy(s_clear_no_layer); s_clear_no_layer = NULL; }
    #endif
}

static void push_clear_prompt(void) {
    if(!s_clear_window) {
        s_clear_window = window_create();
        if (!s_clear_window) return;

        window_set_click_config_provider(s_clear_window, clear_click_config_provider);
        window_set_window_handlers(s_clear_window, (WindowHandlers) {
            .load = clear_window_load, .unload = clear_window_unload,
        });
    }

    #if defined(PBL_PLATFORM_APLITE)
    window_stack_push(s_clear_window, false);
    #else
    if (!window_stack_contains_window(s_clear_window)) window_stack_push(s_clear_window, true);
    #endif
}

static uint16_t get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) { return NUM_SETTINGS_ROWS; }
static int16_t get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) { return ui_get_dynamic_cell_height(); }

static void draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    AppSettings *settings = storage_get_settings(); char subtitle[32];
    bool is_selected = false; int marquee = 0;
    #if !defined(PBL_PLATFORM_APLITE)
    is_selected = (s_selected_index.section == cell_index->section && s_selected_index.row == cell_index->row);
    marquee = s_marquee_offset;
    #endif

    switch (cell_index->row) {
        case ROW_WEIGHT:
            if (settings->use_metric_weight) snprintf(subtitle, sizeof(subtitle), "%d kg", (int)(settings->weight + 0.5f));
            else snprintf(subtitle, sizeof(subtitle), "%d lbs", (int)(settings->weight + 0.5f));
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Weight", subtitle, is_selected, marquee); break;
        case ROW_SEX:
            snprintf(subtitle, sizeof(subtitle), settings->gender_constant > 0.6f ? "Male" : "Female");
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Biological Sex", subtitle, is_selected, marquee); break;
        case ROW_WEIGHT_UNIT:
            snprintf(subtitle, sizeof(subtitle), settings->use_metric_weight ? "Kilograms (kg)" : "Pounds (lbs)");
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Weight Unit", subtitle, is_selected, marquee); break;
        case ROW_VOL_UNIT:
            snprintf(subtitle, sizeof(subtitle), settings->use_metric_volume ? "Metric (ml)" : "Imperial (oz)");
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Volume Unit", subtitle, is_selected, marquee); break;
        case ROW_SPLIT_DRINK:
            snprintf(subtitle, sizeof(subtitle), settings->enable_portions ? "Enabled" : "Disabled");
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Split Drinks", subtitle, is_selected, marquee); break;
            #if defined(PBL_TOUCH)
        case ROW_BACK_GESTURE:
            snprintf(subtitle, sizeof(subtitle), settings->right_handed_mode ? "Right (Swipe ->)" : "Left (Swipe <-)");
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Back Gesture", subtitle, is_selected, marquee); break;
            #endif
        case ROW_TARGET_BAC:
            #if defined(PBL_PLATFORM_APLITE)
            if (settings->target_bac < 0.0f) snprintf(subtitle, sizeof(subtitle), "Disabled");
            else { int target_tenths = (int)(settings->target_bac * 100.0f + 0.5f); snprintf(subtitle, sizeof(subtitle), "%d.%02d%%", target_tenths / 100, target_tenths % 100); }
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Target BAC", subtitle, is_selected, marquee); break;
            #else
            if (settings->target_bac < 0.0f) snprintf(subtitle, sizeof(subtitle), "Disable timeline reporting");
            else { int target_tenths = (int)(settings->target_bac * 100.0f + 0.5f); snprintf(subtitle, sizeof(subtitle), "%d.%02d%%", target_tenths / 100, target_tenths % 100); }
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Target BAC (for timeline)", subtitle, is_selected, marquee); break;
            #endif
            #if !defined(PBL_PLATFORM_APLITE)
        case ROW_REGION:
            if (settings->region == REGION_UK) snprintf(subtitle, sizeof(subtitle), "United Kingdom");
            else if (settings->region == REGION_EU) snprintf(subtitle, sizeof(subtitle), "Europe");
            else if (settings->region == REGION_AU) snprintf(subtitle, sizeof(subtitle), "Australia");
            else snprintf(subtitle, sizeof(subtitle), "North America");
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Region", subtitle, is_selected, marquee); break;
        #endif
        case ROW_IDLE_TIMEOUT:
            if (settings->idle_timeout_mins == 0) snprintf(subtitle, sizeof(subtitle), "Infinite");
            else if (settings->idle_timeout_mins == 1) snprintf(subtitle, sizeof(subtitle), "1 Minute");
            else snprintf(subtitle, sizeof(subtitle), "%d Minutes", settings->idle_timeout_mins);
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Idle Timeout", subtitle, is_selected, marquee); break;
        case ROW_THEME:
            if (settings->theme_mode == THEME_MODE_LIGHT) snprintf(subtitle, sizeof(subtitle), "Light");
            else if (settings->theme_mode == THEME_MODE_DARK) snprintf(subtitle, sizeof(subtitle), "Dark");
            else snprintf(subtitle, sizeof(subtitle), "Auto (6pm - 6am)");
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Theme", subtitle, is_selected, marquee); break;
        case ROW_CLEAR_ALL:
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Clear All Drinks", "Resets BAC to 0.00", is_selected, marquee); break;
    }
}

static void select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    #if !defined(PBL_PLATFORM_APLITE)
    if (s_click_locked) return;
    s_click_locked = true; app_timer_register(300, unlock_click, NULL);
    #endif

    AppSettings *settings = storage_get_settings(); app_reset_idle_timer();

    switch (cell_index->row) {
        case ROW_WEIGHT: weight_window_push(); break;
        case ROW_SEX: settings->gender_constant = (settings->gender_constant > 0.6f) ? 0.55f : 0.68f; storage_save_settings(); menu_layer_reload_data(menu_layer); break;
        case ROW_WEIGHT_UNIT:
            if (settings->use_metric_weight) { settings->weight = settings->weight * 2.20462f; settings->use_metric_weight = false; }
            else { settings->weight = settings->weight / 2.20462f; settings->use_metric_weight = true; }
            storage_save_settings(); menu_layer_reload_data(menu_layer); break;
        case ROW_VOL_UNIT: settings->use_metric_volume = !settings->use_metric_volume; storage_save_settings(); menu_layer_reload_data(menu_layer); break;
        case ROW_SPLIT_DRINK: settings->enable_portions = !settings->enable_portions; storage_save_settings(); menu_layer_reload_data(menu_layer); break;
        #if defined(PBL_TOUCH)
        case ROW_BACK_GESTURE: settings->right_handed_mode = !settings->right_handed_mode; storage_save_settings(); menu_layer_reload_data(menu_layer); break;
        #endif
        case ROW_TARGET_BAC: {
            static const float targets[] = {-1.0f, 0.00f, 0.02f, 0.04f, 0.05f, 0.06f, 0.07f, 0.08f}; int curr = 0;
            for(int i=0; i<8; i++) { if (settings->target_bac >= targets[i] - 0.001f) curr = i; }
            settings->target_bac = targets[(curr + 1) % 8]; storage_save_settings(); menu_layer_reload_data(menu_layer); break;
        }
        #if !defined(PBL_PLATFORM_APLITE)
        case ROW_REGION: settings->region = (settings->region + 1) % 4; storage_save_settings(); menu_layer_reload_data(menu_layer); break;
        #endif
        case ROW_IDLE_TIMEOUT: {
            static const uint8_t timeouts[] = {0, 1, 2, 5, 10, 15}; int curr = 0;
            for(int i=0; i<6; i++) { if (settings->idle_timeout_mins == timeouts[i]) curr = i; }
            settings->idle_timeout_mins = timeouts[(curr + 1) % 6]; storage_save_settings(); menu_layer_reload_data(menu_layer); break;
        }
        case ROW_THEME:
            if (settings->theme_mode == THEME_MODE_LIGHT) settings->theme_mode = THEME_MODE_DARK;
            else if (settings->theme_mode == THEME_MODE_DARK) settings->theme_mode = THEME_MODE_AUTO;
            else settings->theme_mode = THEME_MODE_LIGHT;
            storage_save_settings(); window_set_background_color(s_window, theme_bg());
        menu_layer_set_normal_colors(menu_layer, theme_bg(), theme_text()); menu_layer_set_highlight_colors(menu_layer, theme_highlight_bg(), theme_highlight_text());
        menu_layer_reload_data(menu_layer); break;
        case ROW_CLEAR_ALL:
            push_clear_prompt();
            break;
    }
}

static void selection_changed_callback(struct MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index, void *callback_context) {
    #if !defined(PBL_PLATFORM_APLITE)
    s_selected_index = new_index; s_marquee_offset = 0;
    #endif
    if (new_index.row != old_index.row || new_index.section != old_index.section) {
        s_clear_confirm = false;
    }
    app_reset_idle_timer();
}

static MenuLayerCallbacks s_settings_cbs = {
    .get_num_rows = get_num_rows_callback, .get_cell_height = get_cell_height_callback,
    .draw_row = draw_row_callback, .select_click = select_callback, .selection_changed = selection_changed_callback,
};

static void window_appear(Window *window) {
    s_clear_confirm = false;
    #if !defined(PBL_PLATFORM_APLITE)
    s_click_locked = false;
    s_marquee_offset = 0;
    s_marquee_timer = app_timer_register(100, marquee_timer_callback, NULL);
    #endif
    if(s_menu_layer) {
        window_set_background_color(window, theme_bg()); menu_layer_set_normal_colors(s_menu_layer, theme_bg(), theme_text());
        menu_layer_set_highlight_colors(s_menu_layer, theme_highlight_bg(), theme_highlight_text());
        menu_layer_reload_data(s_menu_layer); touch_menu_subscribe(window, s_menu_layer, s_settings_cbs, NULL);
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
    menu_layer_set_callbacks(s_menu_layer, NULL, s_settings_cbs); menu_layer_set_click_config_onto_window(s_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void window_unload(Window *window) {
    if (s_menu_layer) { menu_layer_destroy(s_menu_layer); s_menu_layer = NULL; }
}

void settings_menu_push(void) {
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
