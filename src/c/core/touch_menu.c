#include <pebble.h>
#include "touch_menu.h"

#ifdef PBL_TOUCH
static Window *s_window = NULL;
static MenuLayer *s_menu = NULL;
static MenuLayerCallbacks s_cbs;
static void *s_ctx = NULL;

static int16_t s_touch_start_y = 0;
static int16_t s_touch_last_y = 0;
static bool s_is_drag = false;
static int16_t s_scroll_velocity = 0;
static AppTimer *s_kinetic_timer = NULL;

static void kill_kinetic_timer(void) {
    if (s_kinetic_timer) {
        app_timer_cancel(s_kinetic_timer);
        s_kinetic_timer = NULL;
    }
}

static void apply_clamped_scroll(ScrollLayer *layer, int16_t delta_y) {
    GPoint offset = scroll_layer_get_content_offset(layer);
    offset.y += delta_y;

    int content_h = scroll_layer_get_content_size(layer).h;
    int layer_h = layer_get_bounds(scroll_layer_get_layer(layer)).size.h;
    int max_scroll = -(content_h - layer_h);
    if (max_scroll > 0) max_scroll = 0;

    if (offset.y > 0) offset.y = 0;
    if (offset.y < max_scroll) offset.y = max_scroll;

    scroll_layer_set_content_offset(layer, offset, false);
}

static void kinetic_timer_callback(void *data) {
    ScrollLayer *scroll_layer = (ScrollLayer *)data;
    if (!scroll_layer) return;

    GPoint old_offset = scroll_layer_get_content_offset(scroll_layer);
    apply_clamped_scroll(scroll_layer, s_scroll_velocity);
    GPoint new_offset = scroll_layer_get_content_offset(scroll_layer);

    s_scroll_velocity = (s_scroll_velocity * 90) / 100;

    if (abs(s_scroll_velocity) <= 1 || old_offset.y == new_offset.y) {
        s_scroll_velocity = 0;
        s_kinetic_timer = NULL;
    } else {
        s_kinetic_timer = app_timer_register(25, kinetic_timer_callback, scroll_layer);
    }
}

static void menu_touch_handler(const TouchEvent *event, void *context) {
    if (!s_menu) return;
    ScrollLayer *scroll_layer = menu_layer_get_scroll_layer(s_menu);

    if (event->type == TouchEvent_Touchdown) {
        s_touch_start_y = event->y;
        s_touch_last_y = event->y;
        s_is_drag = false;
        s_scroll_velocity = 0;
        kill_kinetic_timer();
    } 
    else if (event->type == TouchEvent_PositionUpdate) {
        if (!s_is_drag && abs(event->y - s_touch_start_y) > 15) {
            s_is_drag = true;
        }
        if (s_is_drag) {
            int16_t delta_y = event->y - s_touch_last_y;
            s_scroll_velocity = delta_y;
            apply_clamped_scroll(scroll_layer, delta_y);
            s_touch_last_y = event->y;
        }
    } 
    else if (event->type == TouchEvent_Liftoff) {
        if (!s_is_drag) {
            // Anchor touch translation exactly to the menu bounds to prevent offsets
            Layer *menu_base_layer = menu_layer_get_layer(s_menu);
            GPoint abs_origin = layer_convert_point_to_screen(menu_base_layer, GPointZero);
            int16_t local_y = event->y - abs_origin.y;

            GPoint offset = scroll_layer_get_content_offset(scroll_layer);
            int16_t content_y = local_y - offset.y;

            int16_t current_y = 0;
            uint16_t num_sections = s_cbs.get_num_sections ? s_cbs.get_num_sections(s_menu, s_ctx) : 1;
            bool found = false;

            for (uint16_t s = 0; s < num_sections; s++) {
                int16_t header_h = s_cbs.get_header_height ? s_cbs.get_header_height(s_menu, s, s_ctx) : 0;
                current_y += header_h;
                
                if (content_y < current_y) break; // Ignore taps precisely on section headers

                uint16_t num_rows = s_cbs.get_num_rows(s_menu, s, s_ctx);
                for (uint16_t r = 0; r < num_rows; r++) {
                    MenuIndex idx = {.section = s, .row = r};
                    int16_t row_h = s_cbs.get_cell_height ? s_cbs.get_cell_height(s_menu, &idx, s_ctx) : 44;
                    
                    if (content_y < current_y + row_h) {
                        MenuIndex current_selection = menu_layer_get_selected_index(s_menu);
                        if (current_selection.section == s && current_selection.row == r) {
                            if (s_cbs.select_click) s_cbs.select_click(s_menu, &idx, s_ctx);
                        } else {
                            menu_layer_set_selected_index(s_menu, idx, MenuRowAlignCenter, true);
                        }
                        found = true;
                        break;
                    }
                    current_y += row_h;
                }
                if (found) break;
            }
        } else {
            if (abs(s_scroll_velocity) > 2) {
                s_kinetic_timer = app_timer_register(25, kinetic_timer_callback, scroll_layer);
            }
        }
    }
}

void touch_menu_subscribe(Window *window, MenuLayer *menu_layer, MenuLayerCallbacks cbs, void *ctx) {
    s_window = window;
    s_menu = menu_layer;
    s_cbs = cbs;
    s_ctx = ctx;
    if (touch_service_is_enabled()) {
        touch_service_subscribe(menu_touch_handler, NULL);
    }
}

void touch_menu_unsubscribe(void) {
    if (touch_service_is_enabled()) {
        touch_service_unsubscribe();
    }
    kill_kinetic_timer();
    s_window = NULL;
    s_menu = NULL;
}
#else
void touch_menu_subscribe(Window *window, MenuLayer *menu_layer, MenuLayerCallbacks cbs, void *ctx) {}
void touch_menu_unsubscribe(void) {}
#endif