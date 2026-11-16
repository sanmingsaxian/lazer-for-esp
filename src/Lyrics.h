#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct LyricLine {
    uint32_t startMs = 0;
    String text;
};

struct LyricSheet {
    static constexpr size_t MAX_LINES = 72;
    LyricLine lines[MAX_LINES];
    size_t count = 0;
    uint32_t trackSeed = 0;
};

struct LyricViewLine {
    bool valid = false;
    String text;
    uint32_t startMs = 0;
    uint32_t endMs = 0;
    int relative = 0;
};

struct LyricView {
    static constexpr size_t VISIBLE = 7;
    LyricViewLine lines[VISIBLE];
    int active = -1;       // Visible slot used for highlighting.
    int activeLine = -1;   // Absolute line used to detect lyric changes.
    uint32_t trackSeed = 0;
    bool available = false;
};

class LyricStore {
public:
    LyricStore();
    void clear(uint32_t trackSeed);
    void load(const String& audioPath, uint32_t trackSeed);
    void snapshot(uint32_t positionMs, LyricView& out);
    int activeLine(uint32_t positionMs) const;

private:
    static String sidecarPath(const String& audioPath);
    static uint32_t parseTimestamp(const String& line, int& bracketEnd);
    static void sortSheet(LyricSheet& sheet);

    LyricSheet _sheets[2];
    volatile int _active = 0;
    SemaphoreHandle_t _mutex;
};

extern LyricStore Lyrics;
