#include "board.h"
#include "pins.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_system.h>
#include <esp_timer.h>

namespace lz {

uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void board_backlight_early() {
    gpio_reset_pin((gpio_num_t)pins::LCD_BACKLIGHT);
    gpio_set_direction((gpio_num_t)pins::LCD_BACKLIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pins::LCD_BACKLIGHT, 1);
}

void board_backlight_init() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.timer_num = LEDC_TIMER_0;
    timer.duty_resolution = LEDC_TIMER_8_BIT;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {};
    channel.gpio_num = pins::LCD_BACKLIGHT;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = 255;
    channel.hpoint = 0;
    ledc_channel_config(&channel);
}

void board_set_brightness(uint8_t value) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, value);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

namespace {
adc_oneshot_unit_handle_t s_adc = nullptr;
adc_cali_handle_t s_cali = nullptr;
bool s_adcReady = false;

void batteryInit() {
    adc_oneshot_unit_init_cfg_t unit = {};
    unit.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&unit, &s_adc) != ESP_OK) return;
    adc_oneshot_chan_cfg_t chan = {};
    chan.atten = ADC_ATTEN_DB_12;
    chan.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_oneshot_config_channel(s_adc, ADC_CHANNEL_9, &chan) != ESP_OK) return;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali = {};
    cali.unit_id = ADC_UNIT_1;
    cali.atten = ADC_ATTEN_DB_12;
    cali.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cali, &s_cali) == ESP_OK) s_adcReady = true;
#endif
}
}  // namespace

int board_battery_level() {
    static bool tried = false;
    if (!tried) {
        tried = true;
        batteryInit();
    }
    if (!s_adcReady) return -1;
    int raw = 0;
    if (adc_oneshot_read(s_adc, ADC_CHANNEL_9, &raw) != ESP_OK) return -1;
    int mv = 0;
    if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) return -1;
    // Cardputer battery divider is 1:1 (two equal resistors).
    mv *= 2;
    int level = (mv - 3300) * 100 / (4200 - 3300);
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    return level;
}

const char* board_reset_reason_name(int code) {
    switch (code) {
        case ESP_RST_POWERON: return "power on";
        case ESP_RST_EXT: return "external reset";
        case ESP_RST_SW: return "software reset";
        case ESP_RST_PANIC: return "panic / exception";
        case ESP_RST_INT_WDT: return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "task watchdog";
        case ESP_RST_WDT: return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_BROWNOUT: return "brownout / low voltage";
        case ESP_RST_SDIO: return "SDIO reset";
        default: return "unknown";
    }
}

}  // namespace lz
