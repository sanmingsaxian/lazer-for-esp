#include <Arduino.h>
#include <M5Cardputer.h>
#include "App.h"
#include "AudioEngine.h"
#include "Library.h"
#include "UI.h"

void setup() {
    // Stamp-S3A 背光引脚为gpio38 请定义自己的
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);

    Serial.begin(115200);
    delay(80);
    Serial.println("\nLazer 1.0.2 - Cardputer v1.1 / Stamp-S3A");

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
    Serial.printf("Lazer Cardputer: SD=%d, tracks=%u, audio=%d\n", sdReady,
                  (unsigned)Library.count(), audioReady);
    delay(450);
}

void loop() {
    App.update();
    UI.render();
    delay(1);
}
