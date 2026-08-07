#include <pebble.h>
#include "core/storage.h"
#include "core/bac_math.h"
#include "windows/container_menu.h"
#include "windows/settings_menu.h"

#define DROPOFF_DELAY_SECONDS (12 * 3600)

typedef enum { EDIT_MODE_TIME, EDIT_MODE_ABV, EDIT_MODE_VOL } EditMode;

static Window *s_main_window;
static MenuLayer *s_menu_layer;
static TextLayer *s_bac_layer;
static TextLayer *s_sober_layer;

static Window *s_edit_window;
static MenuLayer *s_edit_menu_layer;
static int s_editing_drink_idx = -1;

static Window *s_value_edit_window;
static TextLayer *s_value_edit_title_layer;
static TextLayer *s_value_edit_layer;
static EditMode s_current_edit_mode;

static float s_current_bac = 0.0f;
static time_t s_sober_time = 0;

// --- Math & Core Logic ---
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

static void update_bac_calculations(void) {
    int num_drinks = storage_get_num_drinks();
    if (num_drinks == 0) {
        s_current_bac = 0.0f;
        s_sober_time = 0;
        return;
    }

    AppSettings *settings = storage_get_settings();
    float weight_in_kg = settings->use_metric_weight ? settings->weight : (settings->weight / 2.20462f);
    UserProfile user = { .weight_kg = weight_in_kg, .gender_constant = settings->gender_constant };

    Drink *drinks = storage_get_drinks();
    time_t current_time = time(NULL);

    s_current_bac = calculate_current_bac(user, drinks, num_drinks, current_time);

    if (s_current_bac > 0.0f) {
        float hours_to_sober = s_current_bac / METABOLISM_RATE_PER_HOUR;
        s_sober_time = current_time + (time_t)(hours_to_sober * 3600.0f);
    } else {
        float bac_at_last_drink = calculate_current_bac(user, drinks, num_drinks, drinks[num_drinks-1].timestamp);
        float hours_to_sober = bac_at_last_drink / METABOLISM_RATE_PER_HOUR;
        s_sober_time = drinks[num_drinks-1].timestamp + (time_t)(hours_to_sober * 3600.0f);
    }
}

static void cleanup_old_drinks(void) {
    if (storage_get_num_drinks() == 0) return;

    update_bac_calculations();
    time_t now = time(NULL);

    if (s_current_bac <= 0.0f && s_sober_time > 0 && now > (s_sober_time + DROPOFF_DELAY_SECONDS)) {
        storage_clear_drinks();
        update_bac_calculations();
    }
}

// --- Color & Theme Logic ---
static GColor get_bac_color(float bac) {
    if (bac <= 0.0f) return GColorMalachite;
    if (bac <= 0.04f) return GColorSpringBud;
    if (bac <= 0.08f) return GColorChromeYellow;
    if (bac <= 0.12f) return GColorSunsetOrange;
    return GColorRed;
}

static void apply_theme_to_menu(Window *window, MenuLayer *menu) {
    window_set_background_color(window, theme_bg());
    menu_layer_set_normal_colors(menu, theme_bg(), theme_text());
    menu_layer_set_highlight_colors(menu, theme_highlight_bg(), theme_highlight_text());
}

static void update_dashboard_text(void) {
    static char s_bac_buffer[16];
    static char s_sober_buffer[32];

    int bac_whole = (int)s_current_bac;
    int bac_thousands = (int)(s_current_bac * 1000.0f) % 1000;
    snprintf(s_bac_buffer, sizeof(s_bac_buffer), "BAC: %d.%03d", bac_whole, bac_thousands);

    if (s_current_bac > 0.0f) {
        struct tm *sober_tm = localtime(&s_sober_time);
        strftime(s_sober_buffer, sizeof(s_sober_buffer), "Sober by %H:%M", sober_tm);
    } else {
        snprintf(s_sober_buffer, sizeof(s_sober_buffer), "Sober");
    }

    if (s_bac_layer) {
        GColor header_bg = PBL_IF_COLOR_ELSE(get_bac_color(s_current_bac), theme_bg());
        GColor header_text = gcolor_legible_over(header_bg);

        text_layer_set_background_color(s_bac_layer, header_bg);
        text_layer_set_text_color(s_bac_layer, header_text);
        text_layer_set_text(s_bac_layer, s_bac_buffer);

        text_layer_set_background_color(s_sober_layer, header_bg);
        text_layer_set_text_color(s_sober_layer, header_text);
        text_layer_set_text(s_sober_layer, s_sober_buffer);
    }
}

// --- Time & Recalculation Engine ---
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    // Recalculate BAC and sober times against current system time every minute
    cleanup_old_drinks();
    update_bac_calculations();
    update_dashboard_text();

    if (s_menu_layer) {
        menu_layer_reload_data(s_menu_layer);
    }
}

// --- Value Edit Window UI (Time, ABV, Volume) ---
static void update_value_edit_text(void) {
    static char s_val_buf[24];
    Drink *drinks = storage_get_drinks();
    Drink *d = &drinks[s_editing_drink_idx];

    if (s_current_edit_mode == EDIT_MODE_TIME) {
        struct tm *tick_time = localtime((time_t*)&d->timestamp);
        strftime(s_val_buf, sizeof(s_val_buf), "%H:%M", tick_time);
    } else if (s_current_edit_mode == EDIT_MODE_ABV) {
        int abv_tenths = (int)(d->abv * 1000.0f + 0.5f);
        int abv_whole = abv_tenths / 10;
        int abv_decimal = abv_tenths % 10;
        snprintf(s_val_buf, sizeof(s_val_buf), "%d.%d%%", abv_whole, abv_decimal);
    } else if (s_current_edit_mode == EDIT_MODE_VOL) {
        snprintf(s_val_buf, sizeof(s_val_buf), "%dml (%doz)", (int)d->volume_ml, (int)(d->volume_ml / 29.5735f));
    }
    text_layer_set_text(s_value_edit_layer, s_val_buf);
}

static void value_edit_up_click_handler(ClickRecognizerRef recognizer, void *context) {
    Drink *drinks = storage_get_drinks();
    Drink *d = &drinks[s_editing_drink_idx];

    if (s_current_edit_mode == EDIT_MODE_TIME) {
        uint32_t remainder = d->timestamp % 300;
        if (remainder == 0) {
            d->timestamp += 300;
        } else {
            d->timestamp += (300 - remainder);
        }
        if (d->timestamp > (uint32_t)time(NULL)) d->timestamp = time(NULL);
    } else if (s_current_edit_mode == EDIT_MODE_ABV) {
        d->abv += 0.001f;
        if (d->abv > 0.99f) d->abv = 0.99f;
    } else if (s_current_edit_mode == EDIT_MODE_VOL) {
        d->volume_ml += 10.0f;
        if (d->volume_ml > 5000.0f) d->volume_ml = 5000.0f;
    }
    update_value_edit_text();
}

static void value_edit_down_click_handler(ClickRecognizerRef recognizer, void *context) {
    Drink *drinks = storage_get_drinks();
    Drink *d = &drinks[s_editing_drink_idx];

    if (s_current_edit_mode == EDIT_MODE_TIME) {
        uint32_t remainder = d->timestamp % 300;
        if (remainder == 0) {
            d->timestamp -= 300;
        } else {
            d->timestamp -= remainder;
        }
    } else if (s_current_edit_mode == EDIT_MODE_ABV) {
        if (d->abv > 0.001f) d->abv -= 0.001f;
        else d->abv = 0.0f;
    } else if (s_current_edit_mode == EDIT_MODE_VOL) {
        d->volume_ml -= 10.0f;
        if (d->volume_ml < 0.0f) d->volume_ml = 0.0f;
    }
    update_value_edit_text();
}

static void value_edit_select_click_handler(ClickRecognizerRef recognizer, void *context) {
    sort_drinks();
    storage_save_drinks(storage_get_drinks(), storage_get_num_drinks());
    window_stack_pop(true);
}

static void value_edit_click_config_provider(void *context) {
    window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, value_edit_up_click_handler);
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, value_edit_down_click_handler);
    window_single_click_subscribe(BUTTON_ID_SELECT, value_edit_select_click_handler);
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

// --- Edit Window UI (Menu) ---
static uint16_t edit_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return 4;
}

static void edit_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    Drink *drinks = storage_get_drinks();
    Drink *d = &drinks[s_editing_drink_idx];
    char subtitle[32];

    switch (cell_index->row) {
        case 0: {
            struct tm *tick_time = localtime((time_t*)&d->timestamp);
            strftime(subtitle, sizeof(subtitle), "%H:%M", tick_time);
            menu_cell_basic_draw(ctx, cell_layer, "Time Finished", subtitle, NULL);
            break;
        }
        case 1: {
            int abv_tenths = (int)(d->abv * 1000.0f + 0.5f);
            int abv_whole = abv_tenths / 10;
            int abv_decimal = abv_tenths % 10;
            snprintf(subtitle, sizeof(subtitle), "%d.%d%%", abv_whole, abv_decimal);
            menu_cell_basic_draw(ctx, cell_layer, "Edit ABV", subtitle, NULL);
            break;
        }
        case 2:
            snprintf(subtitle, sizeof(subtitle), "%d ml (%d oz)", (int)d->volume_ml, (int)(d->volume_ml / 29.5735f));
            menu_cell_basic_draw(ctx, cell_layer, "Edit Volume", subtitle, NULL);
            break;
        case 3:
            menu_cell_basic_draw(ctx, cell_layer, "Delete Drink", "Remove from log", NULL);
            break;
    }
}

static void edit_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    Drink *drinks = storage_get_drinks();
    int num_drinks = storage_get_num_drinks();

    switch (cell_index->row) {
        case 0: s_current_edit_mode = EDIT_MODE_TIME; break;
        case 1: s_current_edit_mode = EDIT_MODE_ABV; break;
        case 2: s_current_edit_mode = EDIT_MODE_VOL; break;
        case 3:
            for (int i = s_editing_drink_idx; i < num_drinks - 1; i++) {
                drinks[i] = drinks[i + 1];
            }
            storage_save_drinks(drinks, num_drinks - 1);
            int discard;
            storage_load_drinks(drinks, &discard);
            window_stack_pop(true);
            return;
    }

    if (cell_index->row < 3) {
        if (!s_value_edit_window) {
            s_value_edit_window = window_create();
            window_set_click_config_provider(s_value_edit_window, value_edit_click_config_provider);
            window_set_window_handlers(s_value_edit_window, (WindowHandlers) {
                .load = value_edit_window_load,
                .unload = value_edit_window_unload,
            });
        }
        window_stack_push(s_value_edit_window, true);
    }
}

static void edit_window_appear(Window *window) {
    if (s_edit_menu_layer) {
        apply_theme_to_menu(window, s_edit_menu_layer);
        menu_layer_reload_data(s_edit_menu_layer);
    }
}

static void edit_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    s_edit_menu_layer = menu_layer_create(bounds);
    apply_theme_to_menu(window, s_edit_menu_layer);

    menu_layer_set_callbacks(s_edit_menu_layer, NULL, (MenuLayerCallbacks){
        .get_num_rows = edit_get_num_rows_callback,
        .draw_row = edit_draw_row_callback,
        .select_click = edit_select_callback,
    });
    menu_layer_set_click_config_onto_window(s_edit_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(s_edit_menu_layer));
}

static void edit_window_unload(Window *window) {
    menu_layer_destroy(s_edit_menu_layer);
    window_destroy(s_edit_window);
    s_edit_window = NULL;
}

// --- Main Window UI ---
static uint16_t main_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
    return 4; // Expanded to 4 to accommodate the clock row
}

static uint16_t main_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    if (section_index == 0) return 1;
    if (section_index == 1) return storage_get_num_drinks();
    if (section_index == 2) return 1;
    if (section_index == 3) return 1; // Time Row
    return 0;
}

static int16_t main_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    if (section_index == 3) return 0; // Time has no header
    return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void main_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
    if (section_index == 0) menu_cell_basic_header_draw(ctx, cell_layer, "Actions");
    else if (section_index == 1 && storage_get_num_drinks() > 0) menu_cell_basic_header_draw(ctx, cell_layer, "Drink Log");
    else if (section_index == 2) menu_cell_basic_header_draw(ctx, cell_layer, "Preferences");
}

static void main_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
    if (cell_index->section == 0) {
        menu_cell_basic_draw(ctx, cell_layer, "Add Drink", "Select Volume & ABV", NULL);
    }
    else if (cell_index->section == 1) {
        Drink *drinks = storage_get_drinks();
        Drink *d = &drinks[cell_index->row];
        char title[32];
        char subtitle[32];

        struct tm *tick_time = localtime((time_t*)&d->timestamp);
        strftime(title, sizeof(title), "%H:%M", tick_time);

        int abv_tenths = (int)(d->abv * 1000.0f + 0.5f);
        int abv_whole = abv_tenths / 10;
        int abv_decimal = abv_tenths % 10;
        snprintf(subtitle, sizeof(subtitle), "%dml (%doz) | %d.%d%%", (int)d->volume_ml, (int)(d->volume_ml / 29.5735f), abv_whole, abv_decimal);

        menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
    }
    else if (cell_index->section == 2) {
        menu_cell_basic_draw(ctx, cell_layer, "Settings", "Weight, Sex, Units", NULL);
    }
    else if (cell_index->section == 3) {
        static char s_time_buffer[16];
        clock_copy_time_string(s_time_buffer, sizeof(s_time_buffer));
        menu_cell_basic_draw(ctx, cell_layer, "Current Time", s_time_buffer, NULL);
    }
}

static void main_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
    if (cell_index->section == 0) {
        container_menu_push();
    } else if (cell_index->section == 1) {
        s_editing_drink_idx = cell_index->row;
        if (!s_edit_window) {
            s_edit_window = window_create();
            window_set_window_handlers(s_edit_window, (WindowHandlers) {
                .load = edit_window_load,
                .appear = edit_window_appear,
                .unload = edit_window_unload,
            });
        }
        window_stack_push(s_edit_window, true);
    } else if (cell_index->section == 2) {
        settings_menu_push();
    }
}

static void main_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    s_bac_layer = text_layer_create(GRect(0, 5, bounds.size.w, 35));
    text_layer_set_font(s_bac_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
    text_layer_set_text_alignment(s_bac_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_bac_layer));

    s_sober_layer = text_layer_create(GRect(0, 35, bounds.size.w, 30));
    text_layer_set_font(s_sober_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
    text_layer_set_text_alignment(s_sober_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_sober_layer));

    GRect menu_bounds = GRect(0, 65, bounds.size.w, bounds.size.h - 65);
    s_menu_layer = menu_layer_create(menu_bounds);
    menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
        .get_num_sections = main_get_num_sections_callback,
        .get_num_rows = main_get_num_rows_callback,
        .get_header_height = main_get_header_height_callback,
        .draw_header = main_draw_header_callback,
        .draw_row = main_draw_row_callback,
        .select_click = main_select_callback,
    });
    menu_layer_set_click_config_onto_window(s_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));

    apply_theme_to_menu(window, s_menu_layer);
    update_bac_calculations();
    update_dashboard_text();
}

static void main_window_appear(Window *window) {
    cleanup_old_drinks();
    update_bac_calculations();
    update_dashboard_text();
    if (s_menu_layer) {
        apply_theme_to_menu(window, s_menu_layer);
        menu_layer_reload_data(s_menu_layer);
    }
}

static void main_window_unload(Window *window) {
    text_layer_destroy(s_bac_layer);
    text_layer_destroy(s_sober_layer);
    menu_layer_destroy(s_menu_layer);
    window_destroy(s_main_window);
    s_main_window = NULL;
}

// --- App Lifecycle ---
static void init(void) {
    int num_drinks = 0;
    Drink drinks[MAX_DRINKS];

    storage_load_settings();
    storage_load_drinks(drinks, &num_drinks);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

    s_main_window = window_create();
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .appear = main_window_appear,
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
