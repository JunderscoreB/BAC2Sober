#pragma once
#include <pebble.h>

// Dynamically scale row heights based on hardware resolution
#ifdef PBL_EMERY
  #define UI_ROW_HEIGHT 60
  #define UI_HEADER_HEIGHT 22
#else
  #define UI_ROW_HEIGHT 44
  #define UI_HEADER_HEIGHT 16
#endif

// Installs kinetic touch scrolling and tap-to-select on any MenuLayer
void touch_menu_subscribe(Window *window, MenuLayer *menu_layer, MenuLayerCallbacks cbs, void *ctx);

// Detaches touch handlers and cleans up memory timers
void touch_menu_unsubscribe(void);