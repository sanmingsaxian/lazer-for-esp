#include <Arduino.h>
#include <M5Cardputer.h>
#include <esp_system.h>
#include "App.h"
#include "AudioEngine.h"
#include "Library.h"
#include "UI.h"
#include "WebControl.h"

void setup() {
    // Stamp-S3A / Cardputer v1.1: GPIO38 supplies the LCD backlight.
    // Enable it before library auto-detection so a failed display probe never
    // leaves the device looking completely powered off.
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);

    Serial.begin(115200);
    delay(80);
    Serial.println("\nLazer " LAZER_VERSION " - Cardputer v1.1 / Stamp-S3A");
    Serial.printf("BOOT_RESET_REASON=%d\n", (int)esp_reset_reason());

    auto config = M5.config();
    M5Cardputer.begin(config, true);
    M5Cardputer.Display.setRotation(0);
    M5Cardputer.Display.setBrightness(255);
    digitalWrite(38, HIGH);
    Serial.printf("SoC=%s, flash=%u MB, LCD=%dx%d\n", ESP.getChipModel(),
                  (unsigned)(ESP.getFlashChipSize() / 1024 / 1024),
                  M5Cardputer.Display.width(), M5Cardputer.Display.height());
    randomSeed(esp_random());

    App.begin();
    UI.begin();

    M5Cardputer.Display.fillScreen(M5Cardputer.Display.color565(12, 9, 14));
    M5Cardputer.Display.setFont(&fonts::efontCN_16);
    M5Cardputer.Display.setTextDatum(MC_DATUM);
    M5Cardputer.Display.setTextColor(M5Cardputer.Display.color565(250, 45, 72));
    M5Cardputer.Display.drawString("Lazer", 120, 54);
    M5Cardputer.Display.setFont(&fonts::efontCN_10);
    M5Cardputer.Display.setTextColor(M5Cardputer.Display.color565(175, 168, 180));
    M5Cardputer.Display.drawString("CARDPUTER V1.1 / STAMP-S3A", 120, 78);

    const bool sdReady = Library.begin();
    const bool audioReady = Audio.begin();
    Web.begin();
    Serial.printf("Lazer Cardputer: SD=%d, tracks=%u, audio=%d\n", sdReady,
                  (unsigned)Library.count(), audioReady);
    delay(450);
}

void loop() {
    Web.update();
    App.update();
    UI.render();
    delay(1);
}
