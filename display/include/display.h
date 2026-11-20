#pragma once
#include <lvgl.h>

namespace lz {

// ST7789V2 240x135 on SPI3 + LVGL v9 display glue.
// Orientation matches M5GFX setRotation(0): MV|MX, gap(40,53), inverted colors.
bool display_begin();
lv_display_t* display_handle();
// Pump LVGL timers; call from the main loop.
void display_poll();

}  // namespace lz
