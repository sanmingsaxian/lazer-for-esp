#include "display.h"
#include "pins.h"

#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_st7789.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <cstdio>

namespace lz {

namespace {
esp_lcd_panel_handle_t s_panel = nullptr;
lv_display_t* s_display = nullptr;

bool onColorTransDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    if (s_display) lv_display_flush_ready(s_display);
    return false;
}

void flushCallback(lv_display_t* display, const lv_area_t* area, uint8_t* pxMap) {
    const int width = area->x2 - area->x1 + 1;
    const int height = area->y2 - area->y1 + 1;
    // ST7789 expects big-endian RGB565; LVGL renders native little-endian.
    lv_draw_sw_rgb565_swap(pxMap, width * height);
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, pxMap);
}

void tickTimer(void*) { lv_tick_inc(2); }
}  // namespace

bool display_begin() {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = pins::LCD_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = pins::LCD_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 240 * 135 * 2;
    if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        printf("DISPLAY_FAIL: spi_bus_initialize\n");
        return false;
    }
    printf("DISPLAY_STEP: spi bus ok\n");

    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = pins::LCD_CS;
    io.dc_gpio_num = pins::LCD_DC;
    io.spi_mode = 0;
    io.pclk_hz = 40 * 1000 * 1000;
    io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    io.on_color_trans_done = onColorTransDone;
    esp_lcd_panel_io_handle_t ioHandle = nullptr;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io, &ioHandle) != ESP_OK) {
        printf("DISPLAY_FAIL: panel_io_spi\n");
        return false;
    }
    printf("DISPLAY_STEP: panel io ok\n");

    esp_lcd_panel_dev_config_t dev = {};
    dev.reset_gpio_num = pins::LCD_RST;
    dev.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    dev.bits_per_pixel = 16;
    if (esp_lcd_new_panel_st7789(ioHandle, &dev, &s_panel) != ESP_OK) {
        printf("DISPLAY_FAIL: new_panel_st7789\n");
        return false;
    }
    printf("DISPLAY_STEP: st7789 panel ok\n");
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_swap_xy(s_panel, true);
    esp_lcd_panel_mirror(s_panel, true, false);
    esp_lcd_panel_set_gap(s_panel, 40, 53);
    // ST7789 powers up with display OFF; the IDF driver's init() does not
    // send DISPON, so it must be issued explicitly.
    esp_lcd_panel_disp_on_off(s_panel, true);
    printf("DISPLAY_STEP: panel configured (MV|MX gap 40/53), display on\n");

    lv_init();
    s_display = lv_display_create(240, 135);
    if (!s_display) return false;
    const size_t bufPixels = 240 * 24;
    void* buf1 = heap_caps_malloc(bufPixels * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void* buf2 = heap_caps_malloc(bufPixels * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1 || !buf2) {
        printf("DISPLAY_FAIL: draw buffer alloc\n");
        return false;
    }
    lv_display_set_buffers(s_display, buf1, buf2, bufPixels * 2,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, flushCallback);

    esp_timer_create_args_t timerArgs = {};
    timerArgs.callback = tickTimer;
    timerArgs.arg = nullptr;
    timerArgs.dispatch_method = ESP_TIMER_TASK;
    timerArgs.name = "lv_tick";
    esp_timer_handle_t timer = nullptr;
    esp_timer_create(&timerArgs, &timer);
    esp_timer_start_periodic(timer, 2000);
    return true;
}

lv_display_t* display_handle() { return s_display; }

void display_poll() { lv_timer_handler(); }

}  // namespace lz
