#include "ui_utils.h"

int16_t ui_get_dynamic_cell_height(void) {
    #if defined(PBL_PLATFORM_APLITE)
    return 44;
    #else
    PreferredContentSize size = preferred_content_size();
    if (size == PreferredContentSizeLarge || size == PreferredContentSizeExtraLarge) return 56;
    if (size == PreferredContentSizeSmall) return 36;
    return 44;
    #endif
}

void ui_draw_dynamic_menu_cell(GContext *ctx, const Layer *cell_layer, const char *title, const char *subtitle, bool is_selected, int marquee_offset) {
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
    } else {
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
    if (bounds.size.w <= 180) {
        int max_w = bounds.size.w - 10;
        if (is_selected && title_size.w > max_w) {
            int overflow = title_size.w - max_w;
            int cycle = overflow * 2 + 20;
            int tick = marquee_offset % cycle;
            if (tick < 10) shift_x = 0;
            else if (tick < 10 + overflow) shift_x = tick - 10;
            else if (tick < 20 + overflow) shift_x = overflow;
            else shift_x = overflow - (tick - (20 + overflow));
        }
        graphics_draw_text(ctx, title, title_font, GRect(5 - shift_x, title_y, max_w + shift_x, title_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        if (subtitle) graphics_draw_text(ctx, subtitle, sub_font, GRect(5, sub_y, bounds.size.w - 10, sub_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    } else {
        int max_w = subtitle ? (bounds.size.w - 50) : (bounds.size.w - 20);
        int title_width = title_size.w + 4;
        if (title_width > max_w) title_width = max_w;

        if (is_selected && title_size.w > max_w) {
            int overflow = title_size.w - max_w;
            int cycle = overflow * 2 + 20;
            int tick = marquee_offset % cycle;
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
        int tick = marquee_offset % cycle;
        if (tick < 10) shift_x = 0;
        else if (tick < 10 + overflow) shift_x = tick - 10;
        else if (tick < 20 + overflow) shift_x = overflow;
        else shift_x = overflow - (tick - (20 + overflow));
    }
    graphics_draw_text(ctx, title, title_font, GRect(5 - shift_x, title_y, max_w + shift_x, title_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    if (subtitle) graphics_draw_text(ctx, subtitle, sub_font, GRect(5, sub_y, bounds.size.w - 10, sub_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    #endif
    #endif
}
