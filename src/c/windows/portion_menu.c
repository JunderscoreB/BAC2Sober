#include <pebble.h>
#include "portion_menu.h"
#include "abv_window.h"
#include "../core/storage.h"
#include "../core/touch_menu.h"

static const float s_volume_steps_pct[] = {
    10.0f, 20.0f, 25.0f, 30.0f, 33.3333f, 40.0f, 50.0f, 
    60.0f, 66.6667f, 70.0f, 75.0f, 80.0f, 90.0f, 100.0f
};
#define NUM_VOLUME_STEPS (sizeof(s_volume_steps_pct) / sizeof(s_volume_steps_pct[0]))

static Window *s_window;
static Layer *s_canvas_layer;
static TextLayer *s_portion_text_layer;

static float s_max_volume_ml;
static float s_current_volume_ml;
static int s_current_step_idx;
static float s_default_abv;
static DrinkShape s_shape;
static int s_edit_drink_idx = -1;

static void update_text_layer(void) {
    static char s_buffer[32];
    AppSettings *settings = storage_get_settings();
    
    int percent = (int)(s_volume_steps_pct[s_current_step_idx] + 0.5f);
    
    if (settings->use_metric_volume) {
        snprintf(s_buffer, sizeof(s_buffer), "%d%% - %dml", percent, (int)(s_current_volume_ml + 0.5f));
    } else {
        snprintf(s_buffer, sizeof(s_buffer), "%d%% - %doz", percent, (int)((s_current_volume_ml / 29.5735f) + 0.5f));
    }
    text_layer_set_text(s_portion_text_layer, s_buffer);
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    int cx = bounds.size.w / 2;
    int cy = (bounds.size.h - 40) / 2;
    float fill_ratio = s_current_volume_ml / s_max_volume_ml;

    bool is_wine = (s_shape == SHAPE_WINE_GLASS || s_shape == SHAPE_WINE_BOTTLE);
    GColor liquid_color = PBL_IF_COLOR_ELSE(is_wine ? GColorDarkCandyAppleRed : (s_shape == SHAPE_SHOT ? GColorRajah : GColorChromeYellow), theme_text());
    GColor glass_color = theme_text();

    static const GPoint s_can_pts[] = {{5,0}, {55,0}, {60,10}, {60,90}, {55,100}, {5,100}, {0,90}, {0,10}}; 
    static const GPoint s_tallboy_pts[] = {{5,0}, {55,0}, {60,10}, {60,130}, {55,140}, {5,140}, {0,130}, {0,10}};
    static const GPoint s_bottle_pts[] = {{15,0}, {29,0}, {29,4}, {28,6}, {28,35}, {40,75}, {40,140}, {36,150}, {8,150}, {4,140}, {4,75}, {16,35}, {16,6}, {15,4}};
    static const GPoint s_wine_bottle_pts[] = {{16,0}, {30,0}, {30,40}, {42,90}, {42,155}, {38,160}, {8,160}, {4,155}, {4,90}, {16,40}};
    static const GPoint s_growler_pts[] = {{30,0}, {50,0}, {50,8}, {46,10}, {46,25}, {75,50}, {70,115}, {60,120}, {20,120}, {10,115}, {5,50}, {34,25}, {34,10}, {30,8}};
    static const GPoint s_pint_pts[] = {{4,0}, {60,0}, {60,85}, {64,100}, {0,100}, {4,85}};
    static const GPoint s_taster_pts[] = {{10,0}, {40,0}, {50,30}, {40,65}, {10,65}, {0,30}};
    static const GPoint s_wine_glass_pts[] = {{10,0}, {50,0}, {60,35}, {32,70}, {28,70}, {0,35}};
    static const GPoint s_shot_pts[] = {{0,0}, {48,0}, {38,50}, {10,50}};

    int h, w, num_pts, neck_h = 0, body_h = 0;
    const GPoint *pts;

    if (s_shape == SHAPE_CAN || s_shape == SHAPE_CUSTOM) { h = 100; w = 60; pts = s_can_pts; num_pts = 8; }
    else if (s_shape == SHAPE_TALLBOY) { h = 140; w = 60; pts = s_tallboy_pts; num_pts = 8; }
    else if (s_shape == SHAPE_BOTTLE) { h = 150; w = 44; pts = s_bottle_pts; num_pts = 14; neck_h=45; body_h=105; }
    else if (s_shape == SHAPE_WINE_BOTTLE) { h = 160; w = 46; pts = s_wine_bottle_pts; num_pts = 10; neck_h=50; body_h=110; }
    else if (s_shape == SHAPE_GROWLER) { h = 120; w = 80; pts = s_growler_pts; num_pts = 14; neck_h=30; body_h=90; }
    else if (s_shape == SHAPE_PINT) { h = 100; w = 64; pts = s_pint_pts; num_pts = 6; }
    else if (s_shape == SHAPE_TASTER) { h = 65; w = 50; pts = s_taster_pts; num_pts = 6; }
    else if (s_shape == SHAPE_WINE_GLASS) { h = 70; w = 60; pts = s_wine_glass_pts; num_pts = 6; }
    else if (s_shape == SHAPE_SHOT) { h = 50; w = 48; pts = s_shot_pts; num_pts = 4; }
    else { h = 100; w = 60; pts = s_can_pts; num_pts = 8; }

    int y_offset = cy - h/2;
    if (s_shape == SHAPE_WINE_BOTTLE || s_shape == SHAPE_BOTTLE) {
        y_offset += 10; 
    }

    if (s_shape == SHAPE_PINT) {
        graphics_context_set_fill_color(ctx, glass_color);
        graphics_fill_rect(ctx, GRect(cx + 20, y_offset + 20, 30, 60), 12, GCornersAll);
        graphics_context_set_fill_color(ctx, theme_bg());
        graphics_fill_rect(ctx, GRect(cx + 24, y_offset + 24, 22, 52), 8, GCornersAll);
    } else if (s_shape == SHAPE_GROWLER) {
        graphics_context_set_fill_color(ctx, glass_color);
        graphics_fill_rect(ctx, GRect(cx + 25, y_offset + 15, 25, 40), 10, GCornersAll);
        graphics_context_set_fill_color(ctx, theme_bg());
        graphics_fill_rect(ctx, GRect(cx + 29, y_offset + 19, 17, 32), 6, GCornersAll);
    }

    GPathInfo path_info = { .num_points = num_pts, .points = (GPoint*)pts };
    GPath *path = gpath_create(&path_info);
    gpath_move_to(path, GPoint(cx - w/2, y_offset));

    graphics_context_set_fill_color(ctx, liquid_color);
    gpath_draw_filled(ctx, path);

    int empty_h;
    if (s_shape == SHAPE_BOTTLE || s_shape == SHAPE_WINE_BOTTLE || s_shape == SHAPE_GROWLER) {
        float neck_vol_pct = 0.10f; 
        if (fill_ratio <= (1.0f - neck_vol_pct)) {
            float body_fill = fill_ratio / (1.0f - neck_vol_pct);
            empty_h = neck_h + body_h - (int)(body_fill * body_h);
        } else {
            float neck_fill = (fill_ratio - (1.0f - neck_vol_pct)) / neck_vol_pct;
            empty_h = neck_h - (int)(neck_fill * neck_h);
        }
    } else {
        empty_h = h - (int)(fill_ratio * h);
    }

    if (empty_h > 0) {
        graphics_context_set_fill_color(ctx, theme_bg());
        graphics_fill_rect(ctx, GRect(cx - w/2 - 2, y_offset - 2, w + 4, empty_h + 2), 0, GCornerNone);
    }

    if (s_shape == SHAPE_WINE_GLASS) {
        graphics_context_set_fill_color(ctx, glass_color);
        graphics_fill_rect(ctx, GRect(cx - 2, y_offset + 70, 4, 40), 0, GCornerNone);
        graphics_fill_rect(ctx, GRect(cx - 15, y_offset + 106, 30, 4), 2, GCornersAll);
    }

    graphics_context_set_stroke_color(ctx, glass_color);
    graphics_context_set_stroke_width(ctx, 3);
    gpath_draw_outline(ctx, path);
    
    graphics_context_set_stroke_width(ctx, 1);
    if (s_shape == SHAPE_CAN || s_shape == SHAPE_TALLBOY || s_shape == SHAPE_CUSTOM) {
        graphics_draw_line(ctx, GPoint(cx - w/2 + 5, y_offset), GPoint(cx + w/2 - 5, y_offset));
    } else if (s_shape == SHAPE_BOTTLE || s_shape == SHAPE_WINE_BOTTLE) {
        graphics_draw_line(ctx, GPoint(cx - 7, y_offset), GPoint(cx + 7, y_offset));
    } else if (s_shape == SHAPE_GROWLER) {
        graphics_draw_line(ctx, GPoint(cx - 10, y_offset), GPoint(cx + 10, y_offset));
    } else if (s_shape == SHAPE_SHOT) {
        graphics_draw_line(ctx, GPoint(cx - w/2, y_offset), GPoint(cx + w/2, y_offset));
    }

    gpath_destroy(path);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_current_step_idx < (int)NUM_VOLUME_STEPS - 1) {
        s_current_step_idx++;
        s_current_volume_ml = s_max_volume_ml * (s_volume_steps_pct[s_current_step_idx] / 100.0f);
        update_text_layer();
        layer_mark_dirty(s_canvas_layer);
    }
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_current_step_idx > 0) {
        s_current_step_idx--;
        s_current_volume_ml = s_max_volume_ml * (s_volume_steps_pct[s_current_step_idx] / 100.0f);
        update_text_layer();
        layer_mark_dirty(s_canvas_layer);
    }
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_edit_drink_idx >= 0) {
        Drink *drinks = storage_get_drinks();
        drinks[s_edit_drink_idx].volume_ml = s_current_volume_ml;
        storage_save_drinks(drinks, storage_get_num_drinks());
        window_stack_pop(true);
    } else {
        abv_window_push(s_current_volume_ml, s_max_volume_ml, s_default_abv, s_shape);
    }
}

static void click_config_provider(void *context) {
    window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, up_click_handler);
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, down_click_handler);
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

#ifdef PBL_TOUCH
static int16_t s_touch_start_y = 0;
static int16_t s_touch_last_y = 0;
static bool s_is_drag = false;

static void touch_handler(const TouchEvent *event, void *context) {
    if (event->type == TouchEvent_Touchdown) {
        s_touch_start_y = event->y;
        s_touch_last_y = event->y;
        s_is_drag = false;
    } else if (event->type == TouchEvent_PositionUpdate) {
        if (!s_is_drag && abs(event->y - s_touch_start_y) > 10) s_is_drag = true;
        
        if (s_is_drag) {
            int16_t delta = event->y - s_touch_last_y;
            if (delta < -15) { 
                up_click_handler(NULL, NULL);
                s_touch_last_y = event->y;
            } else if (delta > 15) { 
                down_click_handler(NULL, NULL);
                s_touch_last_y = event->y;
            }
        }
    } else if (event->type == TouchEvent_Liftoff) {
        if (!s_is_drag) select_click_handler(NULL, NULL); 
    }
}
#endif

static void window_appear(Window *window) {
    #ifdef PBL_TOUCH
    if (touch_service_is_enabled()) {
        touch_service_subscribe(touch_handler, NULL);
    }
    #endif
}

static void window_disappear(Window *window) {
    #ifdef PBL_TOUCH
    if (touch_service_is_enabled()) {
        touch_service_unsubscribe();
    }
    #endif
}

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    window_set_background_color(window, theme_bg());

    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);

    s_portion_text_layer = text_layer_create(GRect(0, bounds.size.h - 40, bounds.size.w, 30));
    text_layer_set_font(s_portion_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
    text_layer_set_text_alignment(s_portion_text_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_portion_text_layer, GColorClear);
    text_layer_set_text_color(s_portion_text_layer, theme_text());
    layer_add_child(window_layer, text_layer_get_layer(s_portion_text_layer));
    
    update_text_layer();
}

static void window_unload(Window *window) {
    layer_destroy(s_canvas_layer);
    text_layer_destroy(s_portion_text_layer);
    window_destroy(s_window);
    s_window = NULL;
}

void portion_menu_push(float max_volume_ml, float current_volume_ml, float default_abv, DrinkShape shape, int edit_drink_idx) {
    s_max_volume_ml = max_volume_ml;
    s_default_abv = default_abv;
    s_shape = shape;
    s_edit_drink_idx = edit_drink_idx;

    float target_pct = (current_volume_ml / max_volume_ml) * 100.0f;
    s_current_step_idx = NUM_VOLUME_STEPS - 1; 
    float min_diff = 100.0f;
    for (int i = 0; i < (int)NUM_VOLUME_STEPS; i++) {
        float diff = s_volume_steps_pct[i] - target_pct;
        if (diff < 0) diff = -diff; 
        if (diff < min_diff) {
            min_diff = diff;
            s_current_step_idx = i;
        }
    }
    s_current_volume_ml = s_max_volume_ml * (s_volume_steps_pct[s_current_step_idx] / 100.0f);

    if(!s_window) {
        s_window = window_create();
        window_set_click_config_provider(s_window, click_config_provider);
        window_set_window_handlers(s_window, (WindowHandlers) {
            .load = window_load,
            .appear = window_appear,
            .disappear = window_disappear,
            .unload = window_unload,
        });
    }
    window_stack_push(s_window, true);
}

void portion_menu_destroy_safe(void) {
    if (s_window && window_stack_contains_window(s_window)) {
        window_stack_remove(s_window, false);
    }
}