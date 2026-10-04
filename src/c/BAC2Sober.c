#include <pebble.h>
#include "core/storage.h"
#include "core/bac_math.h"
#include "core/touch_menu.h"
#include "core/ui_utils.h"
#include "windows/container_menu.h"
#include "windows/settings_menu.h"
#include "windows/portion_menu.h"
#include "windows/big_time_window.h"
#include "windows/abv_window.h"
#include "windows/time_offset_menu.h"
#include "windows/custom_volume_window.h"

typedef enum { EDIT_MODE_TIME, EDIT_MODE_ABV, EDIT_MODE_VOL } EditMode;

static Window *s_main_window = NULL;

#if !defined(PBL_PLATFORM_APLITE)
// --- GLOBAL WIZARD ROUTER STATE ---
int g_wizard_next_step = 0;
float g_wizard_vol = 0;
float g_wizard_orig_vol = 0;
float g_wizard_abv = 0;
DrinkShape g_wizard_shape = SHAPE_CAN;

static void wizard_advance_cb(void *data) {
    int step = g_wizard_next_step;
    if (step == 0) return;
    g_wizard_next_step = 0;

    if (step == 1) portion_menu_push(g_wizard_orig_vol, g_wizard_vol, g_wizard_abv, g_wizard_shape, -1);
    else if (step == 2) abv_window_push(g_wizard_vol, g_wizard_orig_vol, g_wizard_abv, g_wizard_shape);
    else if (step == 3) time_offset_menu_push(g_wizard_vol, g_wizard_orig_vol, g_wizard_abv / 100.0f, g_wizard_shape);
    else if (step == 4) custom_volume_window_push(355.0f, 5.0f);
}

void wizard_advance(void) {
    app_timer_register(50, wizard_advance_cb, NULL);
}
// ----------------------------------
#endif

static MenuLayer *s_menu_layer;
#if defined(PBL_PLATFORM_APLITE)
static Layer *s_dashboard_layer;
#else
static TextLayer *s_bac_layer;
static TextLayer *s_sober_layer;
static TextLayer *s_bac_title_layer;
#endif

static Window *s_edit_window;
static MenuLayer *s_edit_menu_layer;
static int s_editing_drink_idx = -1;

#if !defined(PBL_PLATFORM_APLITE)
static MenuIndex s_edit_selected_index = {0, 0};
static AppTimer *s_edit_marquee_timer = NULL;
static int s_edit_marquee_offset = 0;
static void edit_marquee_timer_callback(void *data) {
    s_edit_marquee_offset += 3;
    if (s_edit_menu_layer) layer_mark_dirty(menu_layer_get_layer(s_edit_menu_layer));
    s_edit_marquee_timer = app_timer_register(100, edit_marquee_timer_callback, NULL);
}
#endif

static Window *s_value_edit_window;
static TextLayer *s_value_edit_title_layer;
static TextLayer *s_value_edit_layer;
static EditMode s_current_edit_mode;

static float s_current_bac = 0.0f;
static time_t s_zero_time = 0;
static time_t s_target_time = 0;
static time_t s_last_pushed_sober_time = 0;
#if !defined(PBL_PLATFORM_APLITE)
static time_t s_last_scheduled_wakeup = 0;
#endif
static time_t s_last_interaction_time = 0;

static MenuLayerCallbacks s_main_menu_cbs;
static MenuLayerCallbacks s_edit_menu_cbs;

#if !defined(PBL_PLATFORM_APLITE)
static bool s_click_locked = false;
static void unlock_click(void *data) { s_click_locked = false; }
#endif

static void oom_pop_callback(void *data) { window_stack_pop(false); }

Window* main_window_get_window(void) { return s_main_window; }
void app_reset_idle_timer(void) { s_last_interaction_time = time(NULL); }

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

#if !defined(PBL_PLATFORM_APLITE)
static void delayed_pin_push_callback(void *data) {
    AppSettings *settings = storage_get_settings();
    if (s_target_time > time(NULL)) send_timeline_pin_update(s_target_time, settings->target_bac);
}
#endif

static void update_bac_calculations(void) {
    int num_drinks = storage_get_num_drinks();
    #if defined(PBL_PLATFORM_APLITE)
    wakeup_cancel_all();
    #endif

    if (num_drinks == 0) {
        s_current_bac = 0.0f; s_zero_time = 0; s_target_time = 0;
        #if !defined(PBL_PLATFORM_APLITE)
        if (s_last_scheduled_wakeup != 0) { wakeup_cancel_all(); s_last_scheduled_wakeup = 0; }
        #endif
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

    if (settings->target_bac >= 0.0f && s_current_bac > settings->target_bac) {
        float hours_to_target = (s_current_bac - settings->target_bac) / METABOLISM_RATE_PER_HOUR;
        s_target_time = current_time + (time_t)(hours_to_target * 3600.0f);

        #if defined(PBL_PLATFORM_APLITE)
        if (s_target_time > current_time) {
            wakeup_schedule(s_target_time, 0, true);
        }
        if (s_target_time != s_last_pushed_sober_time && s_target_time > current_time) {
            send_timeline_pin_update(s_target_time, settings->target_bac);
            s_last_pushed_sober_time = s_target_time;
        }
        #else
        if (s_target_time > current_time && (s_last_scheduled_wakeup == 0 || abs((int)(s_target_time - s_last_scheduled_wakeup)) > 60)) {
            wakeup_cancel_all(); wakeup_schedule(s_target_time, 0, true);
            s_last_scheduled_wakeup = s_target_time;
        }

        if (s_target_time > current_time && (s_last_pushed_sober_time == 0 || abs((int)(s_target_time - s_last_pushed_sober_time)) > 60)) {
            app_timer_register(1000, delayed_pin_push_callback, NULL);
            s_last_pushed_sober_time = s_target_time;
        }
        #endif
    } else {
        s_target_time = 0;
        #if !defined(PBL_PLATFORM_APLITE)
        if (s_last_scheduled_wakeup != 0) { wakeup_cancel_all(); s_last_scheduled_wakeup = 0; }
        #endif
    }
}

static void cleanup_old_drinks(void) {
    if (storage_get_num_drinks() == 0) return;
    update_bac_calculations();
    if (s_current_bac <= 0.0f && time(NULL) >= s_zero_time) {
        storage_clear_drinks(); update_bac_calculations();
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
    if (settings->target_bac >= 0.0f) big_time_window_update(s_target_time, "Target BAC Time", current_time_color);
    else big_time_window_update(s_zero_time, "Sober By Time", current_time_color);
    #else
    if (settings->target_bac >= 0.0f) big_time_window_update(s_target_time, "Target BAC Time");
    else big_time_window_update(s_zero_time, "Sober By Time");
    #endif
}

#if defined(PBL_PLATFORM_APLITE)
static void dashboard_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);

    char bac_buffer[16];
    int bac_whole = (int)s_current_bac;
    int bac_thousands = (int)(s_current_bac * 1000.0f) % 1000;
    snprintf(bac_buffer, sizeof(bac_buffer), "%d.%03d ", bac_whole, bac_thousands);

    char sober_buffer[32];
    AppSettings *settings = storage_get_settings();
    if (settings->target_bac >= 0.0f) {
        if (s_target_time > 0) {
            struct tm *sober_tm = localtime(&s_target_time);
            if (sober_tm) {
                if (settings->target_bac > 0.001f) strftime(sober_buffer, sizeof(sober_buffer), "Target by %H:%M", sober_tm);
                else strftime(sober_buffer, sizeof(sober_buffer), "Sober by %H:%M", sober_tm);
            }
        } else {
            if (settings->target_bac > 0.001f) snprintf(sober_buffer, sizeof(sober_buffer), "Target Reached");
            else snprintf(sober_buffer, sizeof(sober_buffer), "Sober");
        }
    } else {
        if (s_zero_time > time(NULL)) {
            struct tm *sober_tm = localtime(&s_zero_time);
            if (sober_tm) strftime(sober_buffer, sizeof(sober_buffer), "Sober by %H:%M", sober_tm);
        } else snprintf(sober_buffer, sizeof(sober_buffer), "Sober");
    }

    graphics_context_set_text_color(ctx, theme_text());

    int bac_h = 35;
    int title_y = 12;
    int title_w = 45;
    int sober_y = 40;

    graphics_draw_text(ctx, "BAC:", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(5, title_y, title_w, 30), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    graphics_draw_text(ctx, bac_buffer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK),
                       GRect(0, 5, bounds.size.w, bac_h), GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

    graphics_draw_text(ctx, sober_buffer, fonts_get_system_font(FONT_KEY_GOTHIC_24),
                       GRect(0, sober_y, bounds.size.w, 30), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}
#endif

static void update_dashboard_text(void) {
    #if defined(PBL_PLATFORM_APLITE)
    if (s_dashboard_layer) layer_mark_dirty(s_dashboard_layer);
    #else
    if (!s_menu_layer) return;

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
                if (clock_is_24h_style()) {
                    if (settings->target_bac > 0.001f) strftime(s_sober_buffer, sizeof(s_sober_buffer), "Target by %H:%M", sober_tm);
                    else strftime(s_sober_buffer, sizeof(s_sober_buffer), "Sober by %H:%M", sober_tm);
                } else {
                    if (settings->target_bac > 0.001f) strftime(s_sober_buffer, sizeof(s_sober_buffer), "Target by %I:%M%p", sober_tm);
                    else strftime(s_sober_buffer, sizeof(s_sober_buffer), "Sober by %I:%M%p", sober_tm);
                }
            }
        } else {
            if (settings->target_bac > 0.001f) snprintf(s_sober_buffer, sizeof(s_sober_buffer), "Target Reached");
            else snprintf(s_sober_buffer, sizeof(s_sober_buffer), "Sober");
        }
    } else {
        if (s_zero_time > time(NULL)) {
            struct tm *sober_tm = localtime(&s_zero_time);
            if (sober_tm) {
                if (clock_is_24h_style()) strftime(s_sober_buffer, sizeof(s_sober_buffer), "Sober by %H:%M", sober_tm);
                else strftime(s_sober_buffer, sizeof(s_sober_buffer), "Sober by %I:%M%p", sober_tm);
            }
        } else snprintf(s_sober_buffer, sizeof(s_sober_buffer), "Sober");
    }

    if (s_bac_layer) {
        GColor header_bg = get_bac_color(s_current_bac);
        GColor header_text = gcolor_legible_over(header_bg);

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

        if (s_sober_layer) {
            text_layer_set_background_color(s_sober_layer, header_bg);
            text_layer_set_text_color(s_sober_layer, header_text);
            text_layer_set_text(s_sober_layer, s_sober_buffer);
        }
    }
    #endif
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    AppSettings *settings = storage_get_settings();
    if (settings->idle_timeout_mins > 0 && s_last_interaction_time > 0) {
        if (window_stack_get_top_window() != big_time_window_get_window()) {
            if (time(NULL) - s_last_interaction_time >= (time_t)(settings->idle_timeout_mins * 60)) {
                window_stack_pop_all(true); return;
            }
        }
    }
    cleanup_old_drinks(); update_bac_calculations(); update_dashboard_text(); trigger_big_time_window_update();
    if (s_menu_layer) { apply_theme_to_menu(s_main_window, s_menu_layer); menu_layer_reload_data(s_menu_layer); }
}

static void update_value_edit_text(void) {
    if (s_editing_drink_idx < 0 || s_editing_drink_idx >= storage_get_num_drinks()) return;
    if (!s_value_edit_layer) return;
    static char s_val_buf[48];
    Drink *d = &storage_get_drinks()[s_editing_drink_idx];

    if (s_current_edit_mode == EDIT_MODE_TIME) {
        struct tm *tick_time = localtime((time_t*)&d->timestamp);
        if (tick_time) {
            if (clock_is_24h_style()) strftime(s_val_buf, sizeof(s_val_buf), "%H:%M", tick_time);
            else strftime(s_val_buf, sizeof(s_val_buf), "%I:%M%p", tick_time);
        }
    } else if (s_current_edit_mode == EDIT_MODE_ABV) {
        int abv_tenths = (int)(d->abv * 1000.0f + 0.5f);
        snprintf(s_val_buf, sizeof(s_val_buf), "%d.%d%%", abv_tenths / 10, abv_tenths % 10);
    } else if (s_current_edit_mode == EDIT_MODE_VOL) {
        AppSettings *settings = storage_get_settings();
        float oz = (d->volume_ml / 29.5735f) + 0.05f;
        int oz_w = (int)oz; int oz_d = (int)(oz * 10.0f) % 10;
        if (settings->use_metric_volume) snprintf(s_val_buf, sizeof(s_val_buf), "%dml (%d.%doz)", (int)d->volume_ml, oz_w, oz_d);
        else snprintf(s_val_buf, sizeof(s_val_buf), "%d.%doz (%dml)", oz_w, oz_d, (int)d->volume_ml);
    }
    text_layer_set_text(s_value_edit_layer, s_val_buf);
}

static void value_edit_up_click_handler(ClickRecognizerRef recognizer, void *context) {
    app_reset_idle_timer();
    if (s_editing_drink_idx < 0 || s_editing_drink_idx >= storage_get_num_drinks()) return;
    Drink *d = &storage_get_drinks()[s_editing_drink_idx];

    uint16_t repeats = recognizer ? click_number_of_clicks_counted(recognizer) : 0;

    if (s_current_edit_mode == EDIT_MODE_TIME) {
        time_t remainder = d->timestamp % 300;
        d->timestamp += remainder == 0 ? 300 : (300 - remainder);
        if (d->timestamp > time(NULL)) d->timestamp = time(NULL);
    } else if (s_current_edit_mode == EDIT_MODE_ABV) {
        d->abv += (repeats > 11) ? 0.01f : 0.001f;
        if (d->abv > 0.75f) d->abv = 0.75f;
    } else if (s_current_edit_mode == EDIT_MODE_VOL) {
        d->volume_ml += 10.0f;
        if (d->volume_ml > 5000.0f) d->volume_ml = 5000.0f;
    }
    update_value_edit_text();
}

static void value_edit_down_click_handler(ClickRecognizerRef recognizer, void *context) {
    app_reset_idle_timer();
    if (s_editing_drink_idx < 0 || s_editing_drink_idx >= storage_get_num_drinks()) return;
    Drink *d = &storage_get_drinks()[s_editing_drink_idx];

    uint16_t repeats = recognizer ? click_number_of_clicks_counted(recognizer) : 0;

    if (s_current_edit_mode == EDIT_MODE_TIME) {
        time_t remainder = d->timestamp % 300;
        d->timestamp -= remainder == 0 ? 300 : remainder;
    } else if (s_current_edit_mode == EDIT_MODE_ABV) {
        float step = (repeats > 11) ? 0.01f : 0.001f;
        if (d->abv > step) d->abv -= step; else d->abv = 0.0f;
    } else if (s_current_edit_mode == EDIT_MODE_VOL) {
        d->volume_ml -= 10.0f;
        if (d->volume_ml < 0.0f) d->volume_ml = 0.0f;
    }
    update_value_edit_text();
}

static void value_edit_select_click_handler(ClickRecognizerRef recognizer, void *context) {
    app_reset_idle_timer(); sort_drinks();
    storage_save_drinks(storage_get_drinks(), storage_get_num_drinks());
    #if defined(PBL_PLATFORM_APLITE)
    window_stack_pop(false);
    #else
    window_stack_pop(true);
    #endif
}

static void value_edit_click_config_provider(void *context) {
    window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, value_edit_up_click_handler);
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, value_edit_down_click_handler);
    window_single_click_subscribe(BUTTON_ID_SELECT, value_edit_select_click_handler);
}

static void value_edit_window_appear(Window *window) {
    app_reset_idle_timer();
}

static void value_edit_window_disappear(Window *window) {
    // Intentionally empty
}

static void value_edit_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window); GRect bounds = layer_get_bounds(window_layer);
    window_set_background_color(window, theme_bg());

    int title_y = bounds.size.h / 2 - 40;
    int val_y = bounds.size.h / 2 - 10;
    GFont title_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont val_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);

    #if !defined(PBL_PLATFORM_APLITE)
    PreferredContentSize size = preferred_content_size();
    if (size == PreferredContentSizeLarge || size == PreferredContentSizeExtraLarge) {
        title_y = bounds.size.h / 2 - 45; val_y = bounds.size.h / 2 - 15;
        title_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
        val_font = fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
    } else if (size == PreferredContentSizeSmall) {
        title_y = bounds.size.h / 2 - 35; val_y = bounds.size.h / 2 - 5;
        title_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
        val_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    }
    #endif

    s_value_edit_title_layer = text_layer_create(GRect(0, title_y, bounds.size.w, 30));
    s_value_edit_layer = text_layer_create(GRect(0, val_y, bounds.size.w, 40));

    if (!s_value_edit_title_layer || !s_value_edit_layer) {
        app_timer_register(10, oom_pop_callback, NULL);
        return;
    }

    text_layer_set_font(s_value_edit_title_layer, title_font);
    text_layer_set_text_alignment(s_value_edit_title_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_value_edit_title_layer, GColorClear);
    text_layer_set_text_color(s_value_edit_title_layer, theme_text());
    layer_add_child(window_layer, text_layer_get_layer(s_value_edit_title_layer));

    if (s_current_edit_mode == EDIT_MODE_TIME) text_layer_set_text(s_value_edit_title_layer, "Time Finished");
    else if (s_current_edit_mode == EDIT_MODE_ABV) text_layer_set_text(s_value_edit_title_layer, "Adjust ABV");
    else text_layer_set_text(s_value_edit_title_layer, "Adjust Volume");

    text_layer_set_font(s_value_edit_layer, val_font);
    text_layer_set_text_alignment(s_value_edit_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_value_edit_layer, GColorClear);
    text_layer_set_text_color(s_value_edit_layer, theme_text());
    layer_add_child(window_layer, text_layer_get_layer(s_value_edit_layer));

    update_value_edit_text();
}

static void value_edit_window_unload(Window *window) {
    if (s_value_edit_title_layer) { text_layer_destroy(s_value_edit_title_layer); s_value_edit_title_layer = NULL; }
    if (s_value_edit_layer) { text_layer_destroy(s_value_edit_layer); s_value_edit_layer = NULL; }
}

static int16_t edit_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) { return ui_get_dynamic_cell_height(); }
static uint16_t edit_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) { return 5; }

static void edit_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    if (s_editing_drink_idx < 0 || s_editing_drink_idx >= storage_get_num_drinks()) return;
    Drink *d = &storage_get_drinks()[s_editing_drink_idx];
    char subtitle[48];
    AppSettings *settings = storage_get_settings();

    bool is_selected = false; int marquee = 0;
    #if !defined(PBL_PLATFORM_APLITE)
    is_selected = (s_edit_selected_index.section == cell_index->section && s_edit_selected_index.row == cell_index->row);
    marquee = s_edit_marquee_offset;
    #endif

    switch (cell_index->row) {
        case 0: {
            struct tm *tick_time = localtime((time_t*)&d->timestamp);
            if (tick_time) {
                if (clock_is_24h_style()) strftime(subtitle, sizeof(subtitle), "%H:%M", tick_time);
                else strftime(subtitle, sizeof(subtitle), "%I:%M%p", tick_time);
            }
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Time Finished", subtitle, is_selected, marquee); break;
        }
        case 1: {
            int abv_tenths = (int)(d->abv * 1000.0f + 0.5f);
            snprintf(subtitle, sizeof(subtitle), "%d.%d%%", abv_tenths / 10, abv_tenths % 10);
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Edit ABV", subtitle, is_selected, marquee); break;
        }
        case 2: {
            float oz = (d->volume_ml / 29.5735f) + 0.05f;
            int oz_w = (int)oz; int oz_d = (int)(oz * 10.0f) % 10;
            if (settings->use_metric_volume) snprintf(subtitle, sizeof(subtitle), "%d ml (%d.%d oz)", (int)d->volume_ml, oz_w, oz_d);
            else snprintf(subtitle, sizeof(subtitle), "%d.%d oz (%d ml)", oz_w, oz_d, (int)d->volume_ml);
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Edit Volume", subtitle, is_selected, marquee); break;
        }
        case 3: {
            float max_vol = d->original_volume_ml > 0 ? d->original_volume_ml : d->volume_ml;
            float oz = (max_vol / 29.5735f) + 0.05f;
            int oz_w = (int)oz; int oz_d = (int)(oz * 10.0f) % 10;
            if (settings->use_metric_volume) snprintf(subtitle, sizeof(subtitle), "Out of %d ml container", (int)max_vol);
            else snprintf(subtitle, sizeof(subtitle), "Out of %d.%d oz container", oz_w, oz_d);
            ui_draw_dynamic_menu_cell(ctx, cell_layer, "Edit Portion", subtitle, is_selected, marquee); break;
        }
        case 4: ui_draw_dynamic_menu_cell(ctx, cell_layer, "Delete Drink", "Remove from log", is_selected, marquee); break;
    }
}

static void edit_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    #if !defined(PBL_PLATFORM_APLITE)
    if (s_click_locked) return;
    s_click_locked = true; app_timer_register(300, unlock_click, NULL);
    #endif

    app_reset_idle_timer();
    Drink *drinks = storage_get_drinks();
    int num_drinks = storage_get_num_drinks();
    if (s_editing_drink_idx < 0 || s_editing_drink_idx >= num_drinks) return;

    switch (cell_index->row) {
        case 0: s_current_edit_mode = EDIT_MODE_TIME; break;
        case 1: s_current_edit_mode = EDIT_MODE_ABV; break;
        case 2: s_current_edit_mode = EDIT_MODE_VOL; break;
        case 3: {
            Drink *d = &drinks[s_editing_drink_idx];
            float max_vol = d->original_volume_ml > 0 ? d->original_volume_ml : d->volume_ml;
            portion_menu_push(max_vol, d->volume_ml, d->abv, d->shape, s_editing_drink_idx);
            return;
        }
        case 4: {
            for (int i = s_editing_drink_idx; i < num_drinks - 1; i++) drinks[i] = drinks[i + 1];
            storage_save_drinks(drinks, num_drinks - 1);
            #if defined(PBL_PLATFORM_APLITE)
            storage_load_drinks();
            window_stack_pop(false);
            #else
            window_stack_pop(true);
            #endif
            return;
        }
    }

    if (cell_index->row < 3) {
        if (!s_value_edit_window) {
            s_value_edit_window = window_create();
            if (!s_value_edit_window) return;
            window_set_click_config_provider(s_value_edit_window, value_edit_click_config_provider);
            window_set_window_handlers(s_value_edit_window, (WindowHandlers) {
                .load = value_edit_window_load, .appear = value_edit_window_appear,
                .disappear = value_edit_window_disappear, .unload = value_edit_window_unload,
            });
        }
        #if defined(PBL_PLATFORM_APLITE)
        window_stack_push(s_value_edit_window, false);
        #else
        if (!window_stack_contains_window(s_value_edit_window)) window_stack_push(s_value_edit_window, true);
        #endif
    }
}

static void edit_selection_changed_callback(struct MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index, void *callback_context) {
    #if !defined(PBL_PLATFORM_APLITE)
    s_edit_selected_index = new_index; s_edit_marquee_offset = 0;
    #endif
    app_reset_idle_timer();
}

static MenuLayerCallbacks s_edit_menu_cbs = {
    .get_num_rows = edit_get_num_rows_callback, .get_cell_height = edit_get_cell_height_callback,
    .draw_row = edit_draw_row_callback, .select_click = edit_select_callback,
    .selection_changed = edit_selection_changed_callback,
};

static void edit_window_appear(Window *window) {
    app_reset_idle_timer();
    #if !defined(PBL_PLATFORM_APLITE)
    s_edit_marquee_offset = 0;
    s_edit_marquee_timer = app_timer_register(100, edit_marquee_timer_callback, NULL);
    #endif
    if (s_edit_menu_layer) {
        apply_theme_to_menu(window, s_edit_menu_layer); menu_layer_reload_data(s_edit_menu_layer);
        touch_menu_subscribe(window, s_edit_menu_layer, s_edit_menu_cbs, NULL);
    }
}

static void edit_window_disappear(Window *window) {
    #if !defined(PBL_PLATFORM_APLITE)
    if (s_edit_marquee_timer) { app_timer_cancel(s_edit_marquee_timer); s_edit_marquee_timer = NULL; }
    #endif
    touch_menu_unsubscribe();
}

static void edit_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window); GRect bounds = layer_get_bounds(window_layer);
    s_edit_menu_layer = menu_layer_create(bounds);
    if (!s_edit_menu_layer) {
        app_timer_register(10, oom_pop_callback, NULL);
        return;
    }

    apply_theme_to_menu(window, s_edit_menu_layer);
    menu_layer_set_callbacks(s_edit_menu_layer, NULL, s_edit_menu_cbs);
    menu_layer_set_click_config_onto_window(s_edit_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(s_edit_menu_layer));
}

static void edit_window_unload(Window *window) {
    if (s_edit_menu_layer) { menu_layer_destroy(s_edit_menu_layer); s_edit_menu_layer = NULL; }
}

static uint16_t main_get_num_sections_callback(MenuLayer *menu_layer, void *data) { return 4; }
static uint16_t main_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    if (section_index == 0) return 1;
    if (section_index == 1) return storage_get_num_drinks();
    if (section_index == 2) return 1;
    if (section_index == 3) return 1;
    return 0;
}
static int16_t main_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) { return ui_get_dynamic_cell_height(); }
static int16_t main_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    if (section_index == 1 && storage_get_num_drinks() == 0) return 0;
    return section_index == 3 ? 0 : UI_HEADER_HEIGHT;
}

static void main_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
    if (section_index == 0) menu_cell_basic_header_draw(ctx, cell_layer, "Actions");
    else if (section_index == 1 && storage_get_num_drinks() > 0) menu_cell_basic_header_draw(ctx, cell_layer, "Drink Log");
    else if (section_index == 2) menu_cell_basic_header_draw(ctx, cell_layer, "Preferences");
}

static void main_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    if (cell_index->section == 0) {
        ui_draw_dynamic_menu_cell(ctx, cell_layer, "Add Drink", "Select Volume & ABV", false, 0);
    } else if (cell_index->section == 1) {
        Drink *d = &storage_get_drinks()[cell_index->row];
        char title[32]; char subtitle[48]; AppSettings *settings = storage_get_settings();

        struct tm *tick_time = localtime((time_t*)&d->timestamp);
        if (tick_time) {
            if (clock_is_24h_style()) strftime(title, sizeof(title), "%H:%M", tick_time);
            else strftime(title, sizeof(title), "%I:%M%p", tick_time);
        }

        int abv_tenths = (int)(d->abv * 1000.0f + 0.5f);
        float oz = (d->volume_ml / 29.5735f) + 0.05f;
        int oz_w = (int)oz; int oz_d = (int)(oz * 10.0f) % 10;

        if (settings->use_metric_volume) snprintf(subtitle, sizeof(subtitle), "%dml (%d.%doz) | %d.%d%%", (int)d->volume_ml, oz_w, oz_d, abv_tenths / 10, abv_tenths % 10);
        else snprintf(subtitle, sizeof(subtitle), "%d.%doz (%dml) | %d.%d%%", oz_w, oz_d, (int)d->volume_ml, abv_tenths / 10, abv_tenths % 10);

        ui_draw_dynamic_menu_cell(ctx, cell_layer, title, subtitle, false, 0);
    } else if (cell_index->section == 2) {
        ui_draw_dynamic_menu_cell(ctx, cell_layer, "Settings", "Weight, Sex, Units", false, 0);
    } else if (cell_index->section == 3) {
        static char s_time_buffer[16]; clock_copy_time_string(s_time_buffer, sizeof(s_time_buffer));
        ui_draw_dynamic_menu_cell(ctx, cell_layer, "Current Time", s_time_buffer, false, 0);
    }
}

static void main_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    #if !defined(PBL_PLATFORM_APLITE)
    if (s_click_locked) return;
    s_click_locked = true; app_timer_register(300, unlock_click, NULL);
    #endif

    app_reset_idle_timer();
    if (cell_index->section == 0) {
        container_menu_push();
    } else if (cell_index->section == 1) {
        s_editing_drink_idx = cell_index->row;
        if (!s_edit_window) {
            s_edit_window = window_create();
            if (!s_edit_window) return;
            window_set_window_handlers(s_edit_window, (WindowHandlers) {
                .load = edit_window_load, .appear = edit_window_appear,
                .disappear = edit_window_disappear, .unload = edit_window_unload,
            });
        }
        #if defined(PBL_PLATFORM_APLITE)
        window_stack_push(s_edit_window, false);
        #else
        if (!window_stack_contains_window(s_edit_window)) window_stack_push(s_edit_window, true);
        #endif
    } else if (cell_index->section == 2) {
        settings_menu_push();
    } else if (cell_index->section == 3) {
        AppSettings *settings = storage_get_settings();

        #if defined(PBL_PLATFORM_APLITE)
        if (settings->target_bac >= 0.0f) big_time_window_push(s_target_time, "Target BAC Time");
        else big_time_window_push(s_zero_time, "Sober By Time");
        #else
        GColor time_color = get_bac_color(s_current_bac);
        if (settings->target_bac >= 0.0f) big_time_window_push(s_target_time, "Target BAC Time", time_color);
        else big_time_window_push(s_zero_time, "Sober By Time", time_color);
        #endif
    }
}

static void main_selection_changed_callback(struct MenuLayer *menu_layer, MenuIndex new_index, MenuIndex old_index, void *callback_context) {
    app_reset_idle_timer();
}

static MenuLayerCallbacks s_main_menu_cbs = {
    .get_num_sections = main_get_num_sections_callback, .get_num_rows = main_get_num_rows_callback,
    .get_cell_height = main_get_cell_height_callback, .get_header_height = main_get_header_height_callback,
    .draw_header = main_draw_header_callback, .draw_row = main_draw_row_callback,
    .select_click = main_select_callback, .selection_changed = main_selection_changed_callback,
};

static void main_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window); GRect bounds = layer_get_bounds(window_layer);
    window_set_background_color(window, theme_bg());

    #if defined(PBL_PLATFORM_APLITE)
    int menu_y = 65;
    s_dashboard_layer = layer_create(GRect(0, 0, bounds.size.w, menu_y));
    s_menu_layer = menu_layer_create(GRect(0, menu_y, bounds.size.w, bounds.size.h - menu_y));

    if (!s_dashboard_layer || !s_menu_layer) {
        if (s_dashboard_layer) { layer_destroy(s_dashboard_layer); s_dashboard_layer = NULL; }
        if (s_menu_layer) { menu_layer_destroy(s_menu_layer); s_menu_layer = NULL; }
        return;
    }
    layer_set_update_proc(s_dashboard_layer, dashboard_update_proc);
    layer_add_child(window_layer, s_dashboard_layer);

    #elif defined(PBL_ROUND)
    s_bac_title_layer = text_layer_create(GRect(0, 2, bounds.size.w, 16));
    if (s_bac_title_layer) {
        text_layer_set_font(s_bac_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
        text_layer_set_text_alignment(s_bac_title_layer, GTextAlignmentCenter);
        text_layer_set_text(s_bac_title_layer, "BAC");
        layer_add_child(window_layer, text_layer_get_layer(s_bac_title_layer));
    }

    s_bac_layer = text_layer_create(GRect(0, 18, bounds.size.w, 30));
    if (s_bac_layer) {
        text_layer_set_font(s_bac_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
        text_layer_set_text_alignment(s_bac_layer, GTextAlignmentCenter);
        layer_add_child(window_layer, text_layer_get_layer(s_bac_layer));
    }

    int sober_h = (bounds.size.w <= 180) ? 28 : 24;
    s_sober_layer = text_layer_create(GRect(0, 48, bounds.size.w, sober_h));

    int menu_y = 48 + sober_h; GRect menu_bounds = GRect(0, menu_y, bounds.size.w, bounds.size.h - menu_y);
    #else
    int bac_h = 35; int title_y = 12; int title_w = 45; int sober_y = 40; int menu_y = 65;
    const char *bac_font = FONT_KEY_BITHAM_30_BLACK; const char *title_font = FONT_KEY_GOTHIC_14_BOLD;

    if (PBL_PLATFORM_TYPE_CURRENT == PlatformTypeEmery) {
        bac_h = 55; title_y = 18; title_w = 65; sober_y = 60; menu_y = 90;
        bac_font = FONT_KEY_BITHAM_42_BOLD; title_font = FONT_KEY_GOTHIC_24_BOLD;
    }

    s_bac_layer = text_layer_create(GRect(0, 5, bounds.size.w, bac_h));
    if (s_bac_layer) {
        text_layer_set_font(s_bac_layer, fonts_get_system_font(bac_font));
        text_layer_set_text_alignment(s_bac_layer, GTextAlignmentRight);
        layer_add_child(window_layer, text_layer_get_layer(s_bac_layer));
    }

    s_bac_title_layer = text_layer_create(GRect(5, title_y, title_w, 30));
    if (s_bac_title_layer) {
        text_layer_set_font(s_bac_title_layer, fonts_get_system_font(title_font));
        text_layer_set_text_alignment(s_bac_title_layer, GTextAlignmentLeft);
        text_layer_set_text(s_bac_title_layer, "BAC:");
        layer_add_child(window_layer, text_layer_get_layer(s_bac_title_layer));
    }

    s_sober_layer = text_layer_create(GRect(0, sober_y, bounds.size.w, menu_y - sober_y));
    GRect menu_bounds = GRect(0, menu_y, bounds.size.w, bounds.size.h - menu_y);
    #endif

    #if !defined(PBL_PLATFORM_APLITE)
    if (s_sober_layer) {
        text_layer_set_font(s_sober_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
        text_layer_set_text_alignment(s_sober_layer, GTextAlignmentCenter);
        layer_add_child(window_layer, text_layer_get_layer(s_sober_layer));
    }

    s_menu_layer = menu_layer_create(menu_bounds);
    if (!s_menu_layer) {
        if (s_bac_layer) text_layer_set_text(s_bac_layer, "OUT OF RAM");
        if (s_sober_layer) text_layer_set_text(s_sober_layer, "Restart App");
        return;
    }
    #endif

    menu_layer_set_callbacks(s_menu_layer, NULL, s_main_menu_cbs);
    menu_layer_set_click_config_onto_window(s_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));

    apply_theme_to_menu(window, s_menu_layer); update_bac_calculations(); update_dashboard_text();
}

static void main_window_appear(Window *window) {
    #if !defined(PBL_PLATFORM_APLITE)
    s_click_locked = false;
    #endif
    app_reset_idle_timer();
    cleanup_old_drinks(); update_bac_calculations(); update_dashboard_text();
    if (s_menu_layer) { apply_theme_to_menu(window, s_menu_layer); menu_layer_reload_data(s_menu_layer); touch_menu_subscribe(window, s_menu_layer, s_main_menu_cbs, NULL); }
}

static void main_window_disappear(Window *window) { touch_menu_unsubscribe(); }

static void main_window_unload(Window *window) {
    #if defined(PBL_PLATFORM_APLITE)
    if (s_dashboard_layer) { layer_destroy(s_dashboard_layer); s_dashboard_layer = NULL; }
    #else
    if (s_bac_title_layer) { text_layer_destroy(s_bac_title_layer); s_bac_title_layer = NULL; }
    if (s_bac_layer) { text_layer_destroy(s_bac_layer); s_bac_layer = NULL; }
    if (s_sober_layer) { text_layer_destroy(s_sober_layer); s_sober_layer = NULL; }
    #endif
    if (s_menu_layer) { menu_layer_destroy(s_menu_layer); s_menu_layer = NULL; }
}

static void init(void) {
    storage_load_settings();
    #if defined(PBL_PLATFORM_APLITE)
    storage_load_drinks();
    #else
    int num_drinks = 0;
    storage_load_drinks(NULL, &num_drinks);
    #endif

    uint32_t out_size = dict_calc_buffer_size(2, sizeof(uint32_t), sizeof(uint32_t));
    app_message_open(32, out_size);

    app_reset_idle_timer(); tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

    s_main_window = window_create();
    if (!s_main_window) return;

    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load, .appear = main_window_appear,
        .disappear = main_window_disappear, .unload = main_window_unload,
    });

    #if defined(PBL_PLATFORM_APLITE)
    window_stack_push(s_main_window, false);
    #else
    window_stack_push(s_main_window, true);
    #endif
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    if (s_main_window) window_destroy(s_main_window);
    if (s_edit_window) window_destroy(s_edit_window);
    if (s_value_edit_window) window_destroy(s_value_edit_window);
    #if !defined(PBL_PLATFORM_APLITE)
    storage_deinit();
    #endif
}

int main(void) { init(); app_event_loop(); deinit(); }
