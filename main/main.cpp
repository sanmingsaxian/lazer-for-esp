/*
Lazer for cardputer
by ChenhaoZhang
2262881823@qq.com
build by ESP-IDF
2026.11.20
*/

#include <cstdio>

#include "app.h"
#include "audio_engine.h"
#include "board.h"
#include "display.h"
#include "keyboard.h"
#include "library.h"
#include "settings.h"
#include "ui.h"
#include "web.h"

#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <nvs_flash.h>

LV_FONT_DECLARE(font_cn_16);
LV_FONT_DECLARE(font_cn_10);

using namespace lz;

extern "C" void app_main(void) {
    //背光引脚为gpio38
    board_backlight_early();

    printf("\nLazer " LAZER_VERSION " - Cardputer v1.1 / Stamp-S3A (ESP-IDF native)\n");
    printf("BOOT_RESET_REASON=%d\n", (int)esp_reset_reason());

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    board_backlight_init();
    if (!display_begin()) {
        printf("DISPLAY_INIT_FAILED\n");
        return;
    }
    keyboard_begin();

    // Boot splash
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_make(12, 9, 14), 0);
    lv_obj_t* title = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(title, &font_cn_16, 0);
    lv_obj_set_style_text_color(title, lv_color_make(250, 45, 72), 0);
    lv_label_set_text(title, "Lazer");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -14);
    lv_obj_t* subtitle = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(subtitle, &font_cn_10, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_make(175, 168, 180), 0);
    lv_label_set_text(subtitle, "CARDPUTER V1.1 / STAMP-S3A");
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 10);
    const uint32_t splashStart = millis();
    while (millis() - splashStart < 450) {
        display_poll();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    lv_obj_clean(lv_screen_active());

    App.begin();
    ui_begin();

    const bool sdReady = Library.begin();
    const bool audioReady = Audio.begin();
    Web.begin();
    printf("Lazer Cardputer: SD=%d, tracks=%u, audio=%d\n", sdReady,
           (unsigned)Library.count(), audioReady);

    for (;;) {
        Web.update();
        App.update();
        ui_render();
        display_poll();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
