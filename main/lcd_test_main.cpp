//简易的lcd test tool

#include <cstdio>

#include "board.h"
#include "display.h"

#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

LV_FONT_DECLARE(font_cn_16);

using namespace lz;

extern "C" void app_main(void) {
    board_backlight_early();
    printf("\nLazer LCD test - Cardputer v1.1 / Stamp-S3A (original Stamp-S3 compatible)\n");
    printf("BOOT_RESET_REASON=%d\n", (int)esp_reset_reason());
    board_backlight_init();
    // Backlight heartbeat: three slow blinks prove the firmware is executing
    // even when the panel shows nothing.
    for (int i = 0; i < 3; ++i) {
        board_set_brightness(0);
        vTaskDelay(pdMS_TO_TICKS(250));
        board_set_brightness(255);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    printf("LCD_TEST: backlight ok, starting display init\n");
    if (!display_begin()) {
        printf("DISPLAY_INIT_FAILED\n");
        // SOS-style fast blinking signals init failure without a serial link.
        for (;;) {
            for (int i = 0; i < 3; ++i) {
                board_set_brightness(0);
                vTaskDelay(pdMS_TO_TICKS(120));
                board_set_brightness(255);
                vTaskDelay(pdMS_TO_TICKS(120));
            }
            vTaskDelay(pdMS_TO_TICKS(900));
        }
    }
    printf("LCD_TEST: display init ok\n");
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_make(0, 0, 0), 0);

    // Eight colour bars across the top.
    const uint32_t colors[8] = {0xff0000, 0xff8000, 0xffff00, 0x00ff00,
                                0x00ffff, 0x0000ff, 0xff00ff, 0xffffff};
    for (int i = 0; i < 8; ++i) {
        lv_obj_t* bar = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(bar);
        lv_obj_set_pos(bar, i * 30, 0);
        lv_obj_set_size(bar, 30, 24);
        lv_obj_set_style_bg_color(bar, lv_color_hex(colors[i]), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    }
    lv_obj_t* ok = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(ok, &font_cn_16, 0);
    lv_obj_set_style_text_color(ok, lv_color_make(0, 220, 80), 0);
    lv_label_set_text(ok, "LCD OK");
    lv_obj_align(ok, LV_ALIGN_CENTER, 0, -16);
    lv_obj_t* model = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(model, &font_cn_16, 0);
    lv_obj_set_style_text_color(model, lv_color_make(230, 230, 235), 0);
    lv_label_set_text(model, "Stamp-S3A / 240x135");
    lv_obj_align(model, LV_ALIGN_CENTER, 0, 6);
    lv_obj_t* name = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(name, &font_cn_16, 0);
    lv_obj_set_style_text_color(name, lv_color_make(160, 160, 170), 0);
    lv_label_set_text(name, "Lazer display test");
    lv_obj_align(name, LV_ALIGN_CENTER, 0, 28);
    lv_obj_t* dot = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_make(0, 220, 80), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, 0, -6);

    printf("LCD_TEST_READY\n");
    uint32_t lastAlive = 0;
    bool bright = true;
    for (;;) {
        const uint32_t now = millis();
        if (now - lastAlive >= 500) {
            lastAlive = now;
            bright = !bright;
            lv_obj_set_style_bg_opa(dot, bright ? LV_OPA_COVER : LV_OPA_30, 0);
            printf("LCD_TEST_ALIVE uptime=%lu\n", (unsigned long)(now / 1000));
        }
        display_poll();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
