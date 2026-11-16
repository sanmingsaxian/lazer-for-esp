#include <Arduino.h>
#include <M5Cardputer.h>

// Minimal Cardputer v1.1 / Stamp-S3A LCD diagnostic.
// No SD, audio, preferences, canvas, or player code is initialized.
void setup() {
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);

    Serial.begin(115200);
    delay(80);
    Serial.println("\nLazer LCD diagnostic - Stamp-S3A");

    auto config = M5.config();
    M5Cardputer.begin(config, true);

    auto& lcd = M5Cardputer.Display;
    // Match the main UI: upright 240x135 landscape on Cardputer v1.1.
    lcd.setRotation(0);
    lcd.setBrightness(255);
    digitalWrite(38, HIGH);

    lcd.fillScreen(TFT_BLACK);
    const uint16_t bars[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW,
                             TFT_MAGENTA, TFT_CYAN, TFT_WHITE, TFT_DARKGREY};
    for (size_t i = 0; i < sizeof(bars) / sizeof(bars[0]); ++i) {
        lcd.fillRect(i * 30, 0, 30, 12, bars[i]);
    }

    lcd.setTextDatum(MC_DATUM);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.drawString("LCD OK", 120, 52);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString("Stamp-S3A / 240x135", 120, 82);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawString("Lazer display test", 120, 101);

    Serial.printf("LCD_TEST_READY SoC=%s Flash=%uMB LCD=%dx%d\n",
                  ESP.getChipModel(),
                  (unsigned)(ESP.getFlashChipSize() / 1024 / 1024),
                  lcd.width(), lcd.height());
}

void loop() {
    // A blinking dot proves the application is alive without redrawing the panel.
    static bool on = false;
    on = !on;
    M5Cardputer.Display.fillCircle(120, 121, 5, on ? TFT_GREEN : TFT_DARKGREEN);
    Serial.printf("LCD_TEST_ALIVE ms=%lu\n", (unsigned long)millis());
    delay(500);
}
