#include <pebble.h>
#include "core/storage.h"
#include "core/bac_math.h"
#include "core/touch_menu.h"
#include "windows/container_menu.h"
#include "windows/settings_menu.h"
#include "windows/portion_menu.h"
#include "windows/big_time_window.h"

typedef enum { EDIT_MODE_TIME, EDIT_MODE_ABV, EDIT_MODE_VOL } EditMode;

static Window *s_main_window;
static MenuLayer *s_menu_layer;
static TextLayer *s_bac_layer;
static TextLayer *s_sober_layer;

// Universally defined so we can use it on both round and rectangular screens
static TextLayer *s_bac_title_layer;

static Window *s_edit_window;
static MenuLayer *s_edit_menu_layer;
static int s_editing_drink_idx = -1;

static Window *s_value_edit_window;
static TextLayer *s_value_edit_title_layer;
static TextLayer *s_value_edit_layer;
static EditMode s_current_edit_mode;

static float s_current_bac = 0.0f;
static time_t s_zero_time = 0;
static time_t s_target_time = 0;
static time_t s_last_pushed_sober_time = 0;

static time_t s_last_interaction_time = 0;

static MenuLayerCallbacks s_main_menu_cbs;
static MenuLayerCallbacks s_edit_menu_cbs;

Window* main_window_get_window(void) {
    return s_main_window;
}

void app_reset_idle_timer(void) {
    s_last_interaction_time = time(NULL);
}

static void sort_drinks(void) {
    int num_drinks = storage_get_num_drinks();
    Drink *drinks = storage_get_drinks();
    for (int i = 0; i < num_drinks - 1; i++) {
        for (int j = 0; j < num_drinks - i - 1; j++) {
            if (drinks[j].timestamp > drinks[j+1].timestamp) {
                Drink temp = drinks[j];
                drinks[j] = drinks[j+1];
                drinks[j+1] = temp;
            }
        }
    }
}

static void send_timeline_pin_update(time_t sober_timestamp, float target_bac) {
    DictionaryIterator *iter;
    AppMessageResult result = app_message_outbox_begin(&iter);
    if (result == APP_MSG_OK) {
        dict_write_uint32(iter, MESSAGE_KEY_SOBER_TIME, (uint32_t)sober_timestamp);
        dict_write_uint32(iter, MESSAGE_KEY_TARGET_BAC, (uint32_t)(target_bac * 100.0f + 0.5f));
        app_message_outbox_send();
    }
}

static void update_bac_calculations(void) {
    int num_drinks = storage_get_num_drinks();

    wakeup_cancel_all();

    if (num_drinks == 0) {
        s_current_bac = 0.0f;
        s_zero_time = 0;
        s_target_time = 0;
        return;
    }

    AppSettings *settings = storage_get_settings();
    float weight_in_kg = settings->use_metric_weight ? settings->weight : (settings->weight / 2.20462f);
    UserProfile user = { .weight_kg = weight_in_kg, .gender_constant = settings->gender_constant };

    Drink *drinks = storage_get_drinks();
    time_t current_time = time(NULL);

    s_current_bac = calculate_current_bac(user, drinks, num_drinks, current_time);

    if (s_current_bac > 0.0f) {
        float hours_to_zero = s_current_bac / METABOLISM_RATE_PER_HOUR;
        s_zero_time = current_time + (time_t)(hours_to_zero * 3600.0f);
    } else {
        float bac_at_last_drink = calculate_current_bac(user, drinks, num_drinks, drinks[num_drinks-1].timestamp);
        float hours_to_zero = bac_at_last_drink / METABOLISM_RATE_PER_HOUR;
        s_zero_time = drinks[num_drinks-1].timestamp + (time_t)(hours_to_zero * 3600.0f);
    }

    if (settings->target_bac >= 0.0f) {
        if (s_current_bac > settings->target_bac) {
            float hours_to_target = (s_current_bac - settings->target_bac) / METABOLISM_RATE_PER_HOUR;
            s_target_time = current_time + (time_t)(hours_to_target * 3600.0f);

            if (s_target_time > current_time) {
                wakeup_schedule(s_target_time, 0, true);
            }
        } else {
            s_target_time = 0;
        }

        if (s_target_time != s_last_pushed_sober_time && s_target_time > current_time) {
            send_timeline_pin_update(s_target_time, settings->target_bac);
            s_last_pushed_sober_time = s_target_time;
        }
    } else {
        s_target_time = 0;
    }
}

static void cleanup_old_drinks(void) {
    if (storage_get_num_drinks() == 0) return;

    update_bac_calculations();
    time_t now = time(NULL);

    if (s_current_bac <= 0.0f && now >= s_zero_time) {
        storage_clear_drinks();
        update_bac_calculations();
    }
}

#if !defined(PBL_PLATFORM_APLITE)
static GColor get_bac_color(float bac) {
    if (bac <= 0.0f) return GColorMalachite;
    if (bac <= 0.04f) return GColorSpringBud;
    if (bac <= 0.08f) return GColorChromeYellow;
    if (bac <= 0.12f) return GColorSunsetOrange;
    return GColorRed;
}
#endif

static void apply_theme_to_menu(Window *window, MenuLayer *menu) {
    window_set_background_color(window, theme_bg());
    menu_layer_set_normal_colors(menu, theme_bg(), theme_text());
    menu_layer_set_highlight_colors(menu, theme_highlight_bg(), theme_highlight_text());
}

static void trigger_big_time_window_update(void) {
    AppSettings *settings = storage_get_settings();

    #if !defined(PBL_PLATFORM_APLITE)
    GColor current_time_color = get_bac_color(s_current_bac);
    #else
    GColor current_time_color = theme_text();
    #endif

    if (settings->target_bac >= 0.0f) {
        big_time_window_update(s_target_time, "Target BAC Time", current_time_color);
    } else {
        big_time_window_update(s_zero_time, "Sober By Time", current_time_color);
    }
}

static void update_dashboard_text(void) {
    static char s_bac_buffer[16];
    static char s_sober_buffer[32];
    AppSettings *settings = storage_get_settings();

    int bac_whole = (int)s_current_bac;
    int bac_thousands = (int)(s_current_bac * 1000.0f) % 1000;

    #if defined(PBL_ROUND)
    snprintf(s_bac_buffer, sizeof(s_bac_buffer), "%d.%03d", bac_whole, bac_thousands);
    #else
    snprintf(s_bac_buffer, sizeof(s_bac_buffer), "%d.%03d ", bac_whole, bac_thousands);
    #endif

    if (settings->target_bac >= 0.0f) {
        if (s_target_time > 0) {
            struct tm *sober_tm = localtime(&s_target_time);
            if (sober_tm) {
                if (settings->target_bac > 0.001f) {
                    strftime(s_sober_buffer, sizeof(s_sober_buffer), "Target by %H:%M", sober_tm);
                } else {
                    strftime(s_sober_buffer, sizeof(s_sober_buffer), "Sober by %H:%M", sober_tm);
                }
            }
        } else {
            if (settings->target_bac > 0.001f) snprintf(s_sober_buffer, sizeof(s_sober_buffer), "Target Reached");
            else snprintf(s_sober_buffer, sizeof(s_sober_buffer), "Sober");
        }
    } else {
        if (s_zero_time > time(NULL)) {
            struct tm *sober_tm = localtime(&s_zero_time);
            if (sober_tm) strftime(s_sober_buffer, sizeof(s_sober_buffer), "Sober by %H:%M", sober_tm);
        } else {
            snprintf(s_sober_buffer, sizeof(s_sober_buffer), "Sober");
        }
    }

    if (s_bac_layer) {
        #if !defined(PBL_PLATFORM_APLITE)
        GColor header_bg = get_bac_color(s_current_bac);
        GColor header_text = gcolor_legible_over(header_bg);
        #else
        GColor header_bg = theme_bg();
        GColor header_text = theme_text();
        #endif

        if (s_bac_title_layer) {
            #if defined(PBL_ROUND)
            text_layer_set_background_color(s_bac_title_layer, header_bg);
            #else
            text_layer_set_background_color(s_bac_title_layer, GColorClear);
            #endif
            text_layer_set_text_color(s_bac_title_layer, header_text);
        }

        text_layer_set_background_color(s_bac_layer, header_bg);
        text_layer_set_text_color(s_bac_layer, header_text);
        text_layer_set_text(s_bac_layer, s_bac_buffer);

        text_layer_set_background_color(s_sober_layer, header_bg);
        text_layer_set_text_color(s_sober_layer, header_text);
        text_layer_set_text(s_sober_layer, s_sober_buffer);
    }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    AppSettings *settings = storage_get_settings();

    if (settings->idle_timeout_mins > 0 && s_last_interaction_time > 0) {
        if (window_stack_get_top_window() != big_time_window_get_window()) {
            time_t now = time(NULL);
            if (now - s_last_interaction_time >= (time_t)(settings->idle_timeout_mins * 60)) {
                window_stack_pop_all(true);
                return;
            }
        }
    }

    cleanup_old_drinks();
    update_bac_calculations();
    update_dashboard_text();
    trigger_big_time_window_update();

    if (s_menu_layer) {
        apply_theme_to_menu(s_main_window, s_menu_layer);
        menu_layer_reload_data(s_menu_layer);
    }
}

static void update_value_edit_text(void) {
    static char s_val_buf[32];
    Drink *drinks = storage_get_drinks();
    Drink *d = &drinks[s_editing_drink_idx];

    if (s_current_edit_mode == EDIT_MODE_TIME) {
        struct tm *tick_time = localtime((time_t*)&d->timestamp);
        if (tick_time) {
            strftime(s_val_buf, sizeof(s_val_buf), "%H:%M", tick_time);
        }
    } else if (s_current_edit_mode == EDIT_MODE_ABV) {
        int abv_tenths = (int)(d->abv * 1000.0f + 0.5f);
        int abv_whole = abv_tenths / 10;
        int abv_decimal = abv_tenths % 10;
        snprintf(s_val_buf, sizeof(s_val_buf), "%d.%d%%", abv_whole, abv_decimal);
    } else if (s_current_edit_mode == EDIT_MODE_VOL) {
        AppSettings *settings = storage_get_settings();
        float oz = (d->volume_ml / 29.5735f) + 0.05f;
        int oz_w = (int)oz;
        int oz_d = (int)(oz * 10.0f) % 10;
        if (settings->use_metric_volume) {
            snprintf(s_val_buf, sizeof(s_val_buf), "%dml (%d.%doz)", (int)d->volume_ml, oz_w, oz_d);
        } else {
            snprintf(s_val_buf, sizeof(s_val_buf), "%d.%doz (%dml)", oz_w, oz_d, (int)d->volume_ml);
        }
    }
    text_layer_set_text(s_value_edit_layer, s_val_buf);
}

#if defined(PBL_TOUCH)
static uint16_t s_ve_touch_repeat_count = 0;
#endif

static void value_edit_up_click_handler(ClickRecognizerRef recognizer, void *context) {
    app_reset_idle_timer();
    Drink *drinks = storage_get_drinks();
    Drink *d = &drinks[s_editing_drink_idx];

    uint16_t repeats = 0;
    if (recognizer) repeats = click_number_of_clicks_counted(recognizer);
    #if defined(PBL_TOUCH)
    else repeats = s_ve_touch_repeat_count;
    #endif

    if (s_current_edit_mode == EDIT_MODE_TIME) {
        time_t remainder = d->timestamp % 300;
        if (remainder == 0) d->timestamp += 300;
        else d->timestamp += (300 - remainder);
        if (d->timestamp > time(NULL)) d->timestamp = time(NULL);
    } else if (s_current_edit_mode == EDIT_MODE_ABV) {
        float step = (repeats > 15) ? 0.01f : 0.001f;
        d->abv += step;
        if (d->abv > 0.75f) d->abv = 0.75f;
    } else if (s_current_edit_mode == EDIT_MODE_VOL) {
        d->volume_ml += 10.0f;
        if (d->volume_ml > 5000.0f) d->volume_ml = 5000.0f;
    }
    update_value_edit_text();
}

static void value_edit_down_click_handler(ClickRecognizerRef recognizer, void *context) {
    app_reset_idle_timer();
    Drink *drinks = storage_get_drinks();
    Drink *d = &drinks[s_editing_drink_idx];

    uint16_t repeats = 0;
    if (recognizer) repeats = click_number_of_clicks_counted(recognizer);
    #if defined(PBL_TOUCH)
    else repeats = s_ve_touch_repeat_count;
    #endif

    if (s_current_edit_mode == EDIT_MODE_TIME) {
        time_t remainder = d->timestamp % 300;
        if (remainder == 0) d->timestamp -= 300;
        else d->timestamp -= remainder;
    } else if (s_current_edit_mode == EDIT_MODE_ABV) {
        float step = (repeats > 15) ? 0.01f : 0.001f;
        if (d->abv > step) d->abv -= step;
        else d->abv = 0.0f;
    } else if (s_current_edit_mode == EDIT_MODE_VOL) {
        d->volume_ml -= 10.0f;
        if (d->volume_ml < 0.0f) d->volume_ml = 0.0f;
    }
    update_value_edit_text();
}

static void value_edit_select_click_handler(ClickRecognizerRef recognizer, void *context) {
    app_reset_idle_timer();
    sort_drinks();
    storage_save_drinks(storage_get_drinks(), storage_get_num_drinks());
    window_stack_pop(true);
}

static void value_edit_click_config_provider(void *context) {
    window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, value_edit_up_click_handler);
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, value_edit_down_click_handler);
    window_single_click_subscribe(BUTTON_ID_SELECT, value_edit_select_click_handler);
}

#if defined(PBL_TOUCH)
static int16_t s_ve_touch_start_x = 0;
static int16_t s_ve_touch_start_y = 0;
static int16_t s_ve_touch_last_y = 0;
static bool s_ve_is_drag = false;

static void value_edit_touch_handler(const TouchEvent *event, void *context) {
    app_reset_idle_timer();

    if (event->type == TouchEvent_Touchdown) {
        s_ve_touch_start_x = event->x;
        s_ve_touch_start_y = event->y;
        s_ve_touch_last_y = event->y;
        s_ve_touch_repeat_count = 0;
        s_ve_is_drag = false;
    } else if (event->type == TouchEvent_PositionUpdate) {
        if (!s_ve_is_drag && abs(event->y - s_ve_touch_start_y) > 10) s_ve_is_drag = true;

        if (s_ve_is_drag) {
            int16_t delta = event->y - s_ve_touch_last_y;
            if (delta < -15) {
                s_ve_touch_repeat_count++;
                value_edit_up_click_handler(NULL, NULL);
                s_ve_touch_last_y = event->y;
            } else if (delta > 15) {
                s_ve_touch_repeat_count++;
                value_edit_down_click_handler(NULL, NULL);
                s_ve_touch_last_y = event->y;
            }
        }
    } else if (event->type == TouchEvent_Liftoff) {
        int16_t dx = event->x - s_ve_touch_start_x;
        int16_t dy = event->y - s_ve_touch_start_y;

        if (abs(dx) > 40 && abs(dx) > abs(dy)) {
            AppSettings *settings = storage_get_settings();
            bool is_back = settings->right_handed_mode ? (dx > 40) : (dx < -40);
            if (is_back) {
                window_stack_pop(true);
                return;
            }
        }

        if (!s_ve_is_drag) value_edit_select_click_handler(NULL, NULL);
    }
}
#endif

static void value_edit_window_appear(Window *window) {
    app_reset_idle_timer();
    #if defined(PBL_TOUCH)
    if (touch_service_is_enabled()) {
        touch_service_subscribe(value_edit_touch_handler, NULL);
    }
    #endif
}

static void value_edit_window_disappear(Window *window) {
    #if defined(PBL_TOUCH)
    if (touch_service_is_enabled()) {
        touch_service_unsubscribe();
    }
    #endif
}

static void value_edit_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    window_set_background_color(window, theme_bg());

    s_value_edit_title_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 40, bounds.size.w, 30));
    text_layer_set_font(s_value_edit_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_value_edit_title_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_value_edit_title_layer, GColorClear);
    text_layer_set_text_color(s_value_edit_title_layer, theme_text());
    layer_add_child(window_layer, text_layer_get_layer(s_value_edit_title_layer));

    if (s_current_edit_mode == EDIT_MODE_TIME) text_layer_set_text(s_value_edit_title_layer, "Time Finished");
    else if (s_current_edit_mode == EDIT_MODE_ABV) text_layer_set_text(s_value_edit_title_layer, "Adjust ABV");
    else text_layer_set_text(s_value_edit_title_layer, "Adjust Volume");

    s_value_edit_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 10, bounds.size.w, 40));
    text_layer_set_font(s_value_edit_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
    text_layer_set_text_alignment(s_value_edit_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_value_edit_layer, GColorClear);
    text_layer_set_text_color(s_value_edit_layer, theme_text());
    layer_add_child(window_layer, text_layer_get_layer(s_value_edit_layer));

    update_value_edit_text();
}

static void value_edit_window_unload(Window *window) {
    text_layer_destroy(s_value_edit_title_layer);
    text_layer_destroy(s_value_edit_layer);
    window_destroy(s_value_edit_window);
    s_value_edit_window = NULL;
}

static int16_t get_dynamic_cell_height(void) {
    #if defined(PBL_PLATFORM_APLITE)
    return 44;
    #else
    PreferredContentSize size = preferred_content_size();
    if (size == PreferredContentSizeLarge || size == PreferredContentSizeExtraLarge) return 56;
    if (size == PreferredContentSizeSmall) return 36;
    return 44;
    #endif
}

static void draw_dynamic_menu_cell(GContext *ctx, const Layer *cell_layer, const char *title, const char *subtitle) {
    GRect bounds = layer_get_bounds(cell_layer);

    #if defined(PBL_PLATFORM_APLITE)
    GFont title_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont sub_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    int title_y = subtitle ? -4 : (bounds.size.h - 24) / 2 - 4;
    graphics_draw_text(ctx, title, title_font, GRect(5, title_y, bounds.size.w - 10, 32), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    if (subtitle) {
        graphics_draw_text(ctx, subtitle, sub_font, GRect(5, 20, bounds.size.w - 10, 26), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
    #else
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

    #if defined(PBL_ROUND)
    if (bounds.size.w <= 180) { // Chalk (Stacked)
        graphics_draw_text(ctx, title, title_font, GRect(5, title_y, bounds.size.w - 10, title_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        if (subtitle) {
            graphics_draw_text(ctx, subtitle, sub_font, GRect(5, sub_y, bounds.size.w - 10, sub_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        }
    } else { // Gabbro (Side-by-side)
        if (subtitle) {
            GSize title_size = graphics_text_layout_get_content_size(title, title_font, GRect(0, 0, bounds.size.w, title_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
            int title_width = title_size.w + 4;
            if (title_width > bounds.size.w - 50) title_width = bounds.size.w - 50;

            int sub_w = bounds.size.w - title_width - 20;
            if (sub_w < 0) sub_w = 0;

            graphics_draw_text(ctx, title, title_font, GRect(10, bounds.size.h / 2 - (title_h/2) - 4, title_width, title_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
            graphics_draw_text(ctx, subtitle, sub_font, GRect(10 + title_width, bounds.size.h / 2 - (sub_h/2) - 2, sub_w, sub_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
        } else {
            graphics_draw_text(ctx, title, title_font, GRect(10, bounds.size.h / 2 - (title_h/2) - 4, bounds.size.w - 20, title_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        }
    }
    #else
    graphics_draw_text(ctx, title, title_font, GRect(5, title_y, bounds.size.w - 10, title_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    if (subtitle) {
        graphics_draw_text(ctx, subtitle, sub_font, GRect(5, sub_y, bounds.size.w - 10, sub_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
    #endif
    #endif
}

static int16_t edit_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    return get_dynamic_cell_height();
}

static uint16_t edit_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return 5;
}

static void edit_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    Drink *drinks = storage_get_drinks();
    Drink *d = &drinks[s_editing_drink_idx];
    char subtitle[32];
    AppSettings *settings = storage_get_settings();

    switch (cell_index->row) {
        case 0: {
            struct tm *tick_time = localtime((time_t*)&d->timestamp);
            if (tick_time) {
                strftime(subtitle, sizeof(subtitle), "%H:%M", tick_time);
            }
            draw_dynamic_menu_cell(ctx, cell_layer, "Time Finished", subtitle);
            break;
        }
        case 1: {
            int abv_tenths = (int)(d->abv * 1000.0f + 0.5f);
            int abv_whole = abv_tenths / 10;
            int abv_decimal = abv_tenths % 10;
            snprintf(subtitle, sizeof(subtitle), "%d.%d%%", abv_whole, abv_decimal);
            draw_dynamic_menu_cell(ctx, cell_layer, "Edit ABV", subtitle);
            break;
        }
        case 2: {
            float oz = (d->volume_ml / 29.5735f) + 0.05f;
            int oz_w = (int)oz;
            int oz_d = (int)(oz * 10.0f) % 10;
            if (settings->use_metric_volume) {
                snprintf(subtitle, sizeof(subtitle), "%d ml (%d.%d oz)", (int)d->volume_ml, oz_w, oz_d);
            } else {
                snprintf(subtitle, sizeof(subtitle), "%d.%d oz (%d ml)", oz_w, oz_d, (int)d->volume_ml);
            }
            draw_dynamic_menu_cell(ctx, cell_layer, "Edit Volume", subtitle);
            break;
        }
        case 3: {
            float max_vol = d->original_volume_ml > 0 ? d->original_volume_ml : d->volume_ml;
            float oz = (max_vol / 29.5735f) + 0.05f;
            int oz_w = (int)oz;
            int oz_d = (int)(oz * 10.0f) % 10;
            if (settings->use_metric_volume) {
                snprintf(subtitle, sizeof(subtitle), "Out of %d ml container", (int)max_vol);
            } else {
                snprintf(subtitle, sizeof(subtitle), "Out of %d.%d oz container", oz_w, oz_d);
            }
            draw_dynamic_menu_cell(ctx, cell_layer, "Edit Portion", subtitle);
            break;
        }
        case 4:
            draw_dynamic_menu_cell(ctx, cell_layer, "Delete Drink", "Remove from log");
            break;
    }
}

static void edit_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    app_reset_idle_timer();
    Drink *drinks = storage_get_drinks();
    int num_drinks = storage_get_num_drinks();
    Drink *d = &drinks[s_editing_drink_idx];

    switch (cell_index->row) {
        case 0: s_current_edit_mode = EDIT_MODE_TIME; break;
        case 1: s_current_edit_mode = EDIT_MODE_ABV; break;
        case 2: s_current_edit_mode = EDIT_MODE_VOL; break;
        case 3: {
            float max_vol = d->original_volume_ml > 0 ? d->original_volume_ml : d->volume_ml;
            portion_menu_push(max_vol, d->volume_ml, d->abv, d->shape, s_editing_drink_idx);
            return;
        }
        case 4: {
            for (int i = s_editing_drink_idx; i < num_drinks - 1; i++) {
                drinks[i] = drinks[i + 1];
            }
            storage_save_drinks(drinks, num_drinks - 1);
            int discard;
            storage_load_drinks(drinks, &discard);
            window_stack_pop(true);
            return;
        }
    }

    if (cell_index->row < 3) {
        if (!s_value_edit_window) {
            s_value_edit_window = window_create();
            window_set_click_config_provider(s_value_edit_window, value_edit_click_config_provider);
            window_set_window_handlers(s_value_edit_window, (WindowHandlers) {
                .load = value_edit_window_load,
                .appear = value_edit_window_appear,
                .disappear = value_edit_window_disappear,
                .unload = value_edit_window_unload,
            });
        }
        window_stack_push(s_value_edit_window, true);
    }
}

static void edit_selection_changed_callback(struct MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index, void *callback_context) {
    app_reset_idle_timer();
}

static MenuLayerCallbacks s_edit_menu_cbs = {
    .get_num_rows = edit_get_num_rows_callback,
    .get_cell_height = edit_get_cell_height_callback,
    .draw_row = edit_draw_row_callback,
    .select_click = edit_select_callback,
    .selection_changed = edit_selection_changed_callback,
};

static void edit_window_appear(Window *window) {
    app_reset_idle_timer();
    if (s_edit_menu_layer) {
        apply_theme_to_menu(window, s_edit_menu_layer);
        menu_layer_reload_data(s_edit_menu_layer);
        touch_menu_subscribe(window, s_edit_menu_layer, s_edit_menu_cbs, NULL);
    }
}

static void edit_window_disappear(Window *window) {
    touch_menu_unsubscribe();
}

static void edit_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    s_edit_menu_layer = menu_layer_create(bounds);
    apply_theme_to_menu(window, s_edit_menu_layer);

    menu_layer_set_callbacks(s_edit_menu_layer, NULL, s_edit_menu_cbs);
    menu_layer_set_click_config_onto_window(s_edit_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(s_edit_menu_layer));
}

static void edit_window_unload(Window *window) {
    menu_layer_destroy(s_edit_menu_layer);
    window_destroy(s_edit_window);
    s_edit_window = NULL;
}

static uint16_t main_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
    return 4;
}

static uint16_t main_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    if (section_index == 0) return 1;
    if (section_index == 1) return storage_get_num_drinks();
    if (section_index == 2) return 1;
    if (section_index == 3) return 1;
    return 0;
}

static int16_t main_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    return get_dynamic_cell_height();
}

static int16_t main_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    if (section_index == 3) return 0;
    return UI_HEADER_HEIGHT;
}

static void main_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
    if (section_index == 0) menu_cell_basic_header_draw(ctx, cell_layer, "Actions");
    else if (section_index == 1 && storage_get_num_drinks() > 0) menu_cell_basic_header_draw(ctx, cell_layer, "Drink Log");
    else if (section_index == 2) menu_cell_basic_header_draw(ctx, cell_layer, "Preferences");
}

static void main_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    if (cell_index->section == 0) {
        draw_dynamic_menu_cell(ctx, cell_layer, "Add Drink", "Select Volume & ABV");
    }
    else if (cell_index->section == 1) {
        Drink *drinks = storage_get_drinks();
        Drink *d = &drinks[cell_index->row];
        char title[32];
        char subtitle[32];
        AppSettings *settings = storage_get_settings();

        struct tm *tick_time = localtime((time_t*)&d->timestamp);
        if (tick_time) {
            strftime(title, sizeof(title), "%H:%M", tick_time);
        }

        int abv_tenths = (int)(d->abv * 1000.0f + 0.5f);
        int abv_whole = abv_tenths / 10;
        int abv_decimal = abv_tenths % 10;

        float oz = (d->volume_ml / 29.5735f) + 0.05f;
        int oz_w = (int)oz;
        int oz_d = (int)(oz * 10.0f) % 10;

        if (settings->use_metric_volume) {
            snprintf(subtitle, sizeof(subtitle), "%dml (%d.%doz) | %d.%d%%", (int)d->volume_ml, oz_w, oz_d, abv_whole, abv_decimal);
        } else {
            snprintf(subtitle, sizeof(subtitle), "%d.%doz (%dml) | %d.%d%%", oz_w, oz_d, (int)d->volume_ml, abv_whole, abv_decimal);
        }

        draw_dynamic_menu_cell(ctx, cell_layer, title, subtitle);
    }
    else if (cell_index->section == 2) {
        draw_dynamic_menu_cell(ctx, cell_layer, "Settings", "Weight, Sex, Units");
    }
    else if (cell_index->section == 3) {
        static char s_time_buffer[16];
        clock_copy_time_string(s_time_buffer, sizeof(s_time_buffer));
        draw_dynamic_menu_cell(ctx, cell_layer, "Current Time", s_time_buffer);
    }
}

static void main_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    app_reset_idle_timer();
    if (cell_index->section == 0) {
        container_menu_push();
    } else if (cell_index->section == 1) {
        s_editing_drink_idx = cell_index->row;
        if (!s_edit_window) {
            s_edit_window = window_create();
            window_set_window_handlers(s_edit_window, (WindowHandlers) {
                .load = edit_window_load,
                .appear = edit_window_appear,
                .disappear = edit_window_disappear,
                .unload = edit_window_unload,
            });
        }
        window_stack_push(s_edit_window, true);
    } else if (cell_index->section == 2) {
        settings_menu_push();
    } else if (cell_index->section == 3) {
        AppSettings *settings = storage_get_settings();

        #if !defined(PBL_PLATFORM_APLITE)
        GColor current_time_color = get_bac_color(s_current_bac);
        #else
        GColor current_time_color = theme_text();
        #endif

        if (settings->target_bac >= 0.0f) {
            big_time_window_push(s_target_time, "Target BAC Time", current_time_color);
        } else {
            big_time_window_push(s_zero_time, "Sober By Time", current_time_color);
        }
    }
}

static void main_selection_changed_callback(struct MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index, void *callback_context) {
    app_reset_idle_timer();
}

static MenuLayerCallbacks s_main_menu_cbs = {
    .get_num_sections = main_get_num_sections_callback,
    .get_num_rows = main_get_num_rows_callback,
    .get_cell_height = main_get_cell_height_callback,
    .get_header_height = main_get_header_height_callback,
    .draw_header = main_draw_header_callback,
    .draw_row = main_draw_row_callback,
    .select_click = main_select_callback,
    .selection_changed = main_selection_changed_callback,
};

static void main_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    // Universally separate the "BAC" label across all platforms
    #if defined(PBL_ROUND)
    s_bac_title_layer = text_layer_create(GRect(0, 2, bounds.size.w, 16));
    text_layer_set_font(s_bac_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
    text_layer_set_text_alignment(s_bac_title_layer, GTextAlignmentCenter);
    text_layer_set_text(s_bac_title_layer, "BAC");
    layer_add_child(window_layer, text_layer_get_layer(s_bac_title_layer));

    s_bac_layer = text_layer_create(GRect(0, 18, bounds.size.w, 30));
    text_layer_set_font(s_bac_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
    text_layer_set_text_alignment(s_bac_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_bac_layer));

    // Add 4 pixels of bottom padding specifically for Chalk (180px width)
    int sober_h = (bounds.size.w <= 180) ? 28 : 24;
    s_sober_layer = text_layer_create(GRect(0, 48, bounds.size.w, sober_h));

    int menu_y = 48 + sober_h;
    GRect menu_bounds = GRect(0, menu_y, bounds.size.w, bounds.size.h - menu_y);
    #else
    int bac_h = 35;
    int title_y = 12;
    int title_w = 45;
    int sober_y = 40;
    int menu_y = 65;
    const char *bac_font = FONT_KEY_BITHAM_30_BLACK;
    const char *title_font = FONT_KEY_GOTHIC_14_BOLD;

    #if !defined(PBL_PLATFORM_APLITE)
    // Scale everything up substantially for the large Emery screen
    if (PBL_PLATFORM_TYPE_CURRENT == PlatformTypeEmery) {
        bac_h = 55;
        title_y = 18;
        title_w = 65;
        sober_y = 60;
        menu_y = 90;
        bac_font = FONT_KEY_BITHAM_42_BOLD;
        title_font = FONT_KEY_GOTHIC_24_BOLD;
    }
    #endif

    // On rectangular screens, the numbers act as the full-width background color band
    s_bac_layer = text_layer_create(GRect(0, 5, bounds.size.w, bac_h));
    text_layer_set_font(s_bac_layer, fonts_get_system_font(bac_font));
    text_layer_set_text_alignment(s_bac_layer, GTextAlignmentRight);
    layer_add_child(window_layer, text_layer_get_layer(s_bac_layer));

    // The "BAC" title sits cleanly on top of the left edge with a transparent background
    s_bac_title_layer = text_layer_create(GRect(5, title_y, title_w, 30));
    text_layer_set_font(s_bac_title_layer, fonts_get_system_font(title_font));
    text_layer_set_text_alignment(s_bac_title_layer, GTextAlignmentLeft);
    text_layer_set_text(s_bac_title_layer, "BAC:");
    layer_add_child(window_layer, text_layer_get_layer(s_bac_title_layer));

    s_sober_layer = text_layer_create(GRect(0, sober_y, bounds.size.w, menu_y - sober_y));
    GRect menu_bounds = GRect(0, menu_y, bounds.size.w, bounds.size.h - menu_y);
    #endif

    text_layer_set_font(s_sober_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
    text_layer_set_text_alignment(s_sober_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_sober_layer));

    s_menu_layer = menu_layer_create(menu_bounds);
    menu_layer_set_callbacks(s_menu_layer, NULL, s_main_menu_cbs);
    menu_layer_set_click_config_onto_window(s_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));

    apply_theme_to_menu(window, s_menu_layer);
    update_bac_calculations();
    update_dashboard_text();
}

static void main_window_appear(Window *window) {
    app_reset_idle_timer();
    cleanup_old_drinks();
    update_bac_calculations();
    update_dashboard_text();
    if (s_menu_layer) {
        apply_theme_to_menu(window, s_menu_layer);
        menu_layer_reload_data(s_menu_layer);
        touch_menu_subscribe(window, s_menu_layer, s_main_menu_cbs, NULL);
    }
}

static void main_window_disappear(Window *window) {
    touch_menu_unsubscribe();
}

static void main_window_unload(Window *window) {
    if (s_bac_title_layer) {
        text_layer_destroy(s_bac_title_layer);
    }
    text_layer_destroy(s_bac_layer);
    text_layer_destroy(s_sober_layer);
    menu_layer_destroy(s_menu_layer);
    window_destroy(s_main_window);
    s_main_window = NULL;
}

static void init(void) {
    int num_drinks = 0;
    Drink drinks[MAX_DRINKS];

    storage_load_settings();
    storage_load_drinks(drinks, &num_drinks);

    app_message_open(APP_MESSAGE_INBOX_SIZE_MINIMUM, APP_MESSAGE_OUTBOX_SIZE_MINIMUM);

    app_reset_idle_timer();
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

    s_main_window = window_create();
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .appear = main_window_appear,
        .disappear = main_window_disappear,
        .unload = main_window_unload,
    });
    window_stack_push(s_main_window, true);
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
