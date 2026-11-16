#include "Lyrics.h"
#include <algorithm>

LyricStore Lyrics;

LyricStore::LyricStore() {
    _mutex = xSemaphoreCreateMutex();
}

void LyricStore::clear(uint32_t trackSeed) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        LyricSheet& sheet = _sheets[1 - _active];
        sheet.count = 0;
        sheet.trackSeed = trackSeed;
        _active = 1 - _active;
        xSemaphoreGive(_mutex);
    }
}

String LyricStore::sidecarPath(const String& audioPath) {
    String path = audioPath;
    int dot = path.lastIndexOf('.');
    if (dot >= 0) path = path.substring(0, dot);
    return path + ".lrc";
}

uint32_t LyricStore::parseTimestamp(const String& line, int& bracketEnd) {
    bracketEnd = line.indexOf(']');
    if (line.length() < 9 || line[0] != '[' || bracketEnd < 8) return UINT32_MAX;
    int colon = line.indexOf(':');
    if (colon != 3) return UINT32_MAX;
    int minutes = line.substring(1, 3).toInt();
    float seconds = line.substring(4, bracketEnd).toFloat();
    if (minutes < 0 || seconds < 0 || seconds >= 60.0f) return UINT32_MAX;
    return (uint32_t)(minutes * 60000.0f + seconds * 1000.0f);
}

void LyricStore::sortSheet(LyricSheet& sheet) {
    std::sort(sheet.lines, sheet.lines + sheet.count,
              [](const LyricLine& a, const LyricLine& b) { return a.startMs < b.startMs; });
}

void LyricStore::load(const String& audioPath, uint32_t trackSeed) {
    clear(trackSeed);
    File file = SD.open(sidecarPath(audioPath), FILE_READ);
    if (!file) return;

    LyricSheet next;
    next.trackSeed = trackSeed;
    while (file.available() && next.count < LyricSheet::MAX_LINES) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() < 10 || line[0] != '[') continue;
        int cursor = 0;
        uint32_t stamps[6];
        size_t stampCount = 0;
        while (cursor < (int)line.length() && line[cursor] == '[' && stampCount < 6) {
            int end = 0;
            String part = line.substring(cursor);
            uint32_t stamp = parseTimestamp(part, end);
            if (stamp == UINT32_MAX) break;
            stamps[stampCount++] = stamp;
            cursor += end + 1;
        }
        if (!stampCount) continue;
        String text = line.substring(cursor);
        int meta = text.indexOf('<');
        if (meta >= 0) text = text.substring(0, meta);
        text.trim();
        if (!text.length()) text = "♪";
        for (size_t i = 0; i < stampCount && next.count < LyricSheet::MAX_LINES; ++i) {
            next.lines[next.count].startMs = stamps[i];
            next.lines[next.count].text = text;
            ++next.count;
        }
    }
    file.close();
    sortSheet(next);

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _sheets[1 - _active] = next;
        _active = 1 - _active;
        xSemaphoreGive(_mutex);
    }
}

int LyricStore::activeLine(uint32_t positionMs) const {
    const LyricSheet& sheet = _sheets[_active];
    int active = -1;
    for (size_t i = 0; i < sheet.count; ++i) {
        if (sheet.lines[i].startMs <= positionMs) active = i;
        else break;
    }
    return active;
}

void LyricStore::snapshot(uint32_t positionMs, LyricView& out) {
    out = LyricView();
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    const LyricSheet& sheet = _sheets[_active];
    out.trackSeed = sheet.trackSeed;
    out.available = sheet.count > 0;
    int active = -1;
    for (size_t i = 0; i < sheet.count; ++i) {
        if (sheet.lines[i].startMs <= positionMs) active = i;
        else break;
    }
    if (active < 0 && sheet.count) active = 0;
    out.active = active;
    out.activeLine = active;
    const int first = std::max(0, active - 3);
    for (size_t slot = 0; slot < LyricView::VISIBLE; ++slot) {
        int index = first + (int)slot;
        if (index < 0 || index >= (int)sheet.count) continue;
        auto& dst = out.lines[slot];
        const auto& src = sheet.lines[index];
        dst.valid = true;
        dst.text = src.text;
        dst.startMs = src.startMs;
        dst.endMs = index + 1 < (int)sheet.count ? sheet.lines[index + 1].startMs : src.startMs + 5000;
        dst.relative = index - active;
        if (index == active) out.active = (int)slot;
    }
    xSemaphoreGive(_mutex);
}
