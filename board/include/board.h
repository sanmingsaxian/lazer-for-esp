#pragma once
#include <cstdint>

namespace lz {

// Milliseconds since boot (esp_timer based, wraps like Arduino millis()).
uint32_t millis();

// Drive GPIO38 high before any display probe so a failed init never looks
// like a dead device (matches original setup() ordering).
void board_backlight_early();

// LEDC PWM backlight; call after early init.
void board_backlight_init();
void board_set_brightness(uint8_t value);  // 0..255

// Battery percentage 0..100, or -1 when the ADC cannot be read.
int board_battery_level();

const char* board_reset_reason_name(int code);

}  // namespace lz
