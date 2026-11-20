#include "lyrics.h"
#include "storage.h"
#include "strutil.h"

#include <cstdio>

namespace lz {

LyricStore Lyrics;

LyricStore::LyricStore() { _mutex = xSemaphoreCreateMutex(); }

std::string LyricStore::sidecarPath(const std::string& audioPath) {
    const size_t dot = audioPath.rfind('.');
    const std::string stem = dot == std::string::npos ? audioPath : audioPath.substr(0, dot);
    return stem + ".lrc";
}

void LyricStore::publish(std::vector<LrcLine>&& lines, uint32_t trackSeed) {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) return;
    const int inactive = 1 - _active;
    _sheets[inactive] = std::move(lines);
    _seeds[inactive] = trackSeed;
    _active = inactive;
    xSemaphoreGive(_mutex);
}

void LyricStore::clear(uint32_t trackSeed) { publish({}, trackSeed); }

void LyricStore::load(const std::string& audioPath, uint32_t trackSeed) {
    const std::string path = storage_vfs_path(sidecarPath(audioPath));
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        clear(trackSeed);
        return;
    }
    std::string text;
    char buffer[1024];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        text.append(buffer, count);
        if (text.size() > 96 * 1024) break;  // same ceiling as WebUI text files
    }
    fclose(file);
    publish(parseLrc(text), trackSeed);
}

void LyricStore::loadText(const std::string& lrc, uint32_t trackSeed) {
    publish(parseLrc(lrc), trackSeed);
}

void LyricStore::snapshot(uint32_t positionMs, LyricSnapshot& out) {
    out = LyricSnapshot{};
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    const int active = _active;
    out.trackSeed = _seeds[active];
    lrcSnapshot(_sheets[active], positionMs, out.view);
    xSemaphoreGive(_mutex);
}

int LyricStore::activeLine(uint32_t positionMs) {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return -1;
    const int line = activeLrcLine(_sheets[_active], positionMs);
    xSemaphoreGive(_mutex);
    return line;
}

}  // namespace lz
