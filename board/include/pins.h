#pragma once

// Lazer Cardputer v1.1 / Stamp-S3A pin map (verified against M5GFX/M5Unified
// board definitions and the original firmware README).
namespace lz::pins {

// ST7789V2 240x135 LCD (SPI2)
constexpr int LCD_SCLK = 36;
constexpr int LCD_MOSI = 35;
constexpr int LCD_DC = 34;
constexpr int LCD_CS = 37;
constexpr int LCD_RST = 33;
constexpr int LCD_BACKLIGHT = 38;

// 74HC138 keyboard matrix
constexpr int KB_A0 = 8;
constexpr int KB_A1 = 9;
constexpr int KB_A2 = 11;
constexpr int KB_INPUTS[7] = {13, 15, 3, 4, 5, 6, 7};

// NS4168 I2S amplifier
constexpr int I2S_BCLK = 41;
constexpr int I2S_LRCK = 43;
constexpr int I2S_DOUT = 42;

// microSD (SPI3)
constexpr int SD_SCK = 40;
constexpr int SD_MISO = 39;
constexpr int SD_MOSI = 14;
constexpr int SD_CS = 12;

// Battery voltage divider (ADC1_CH9)
constexpr int BATTERY_ADC_GPIO = 10;

}  // namespace lz::pins
