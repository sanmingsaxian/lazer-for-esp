#pragma once
#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "lrc.h"

namespace lz {

struct LyricSnapshot {
    LrcView view;
    uint32_t trackSeed = 0;
};

// Double-buffered lyric store: the audio task loads while the UI reads.
class LyricStore {
public:
    LyricStore();
    void clear(uint32_t trackSeed);
    void load(const std::string& audioPath, uint32_t trackSeed);
    void loadText(const std::string& lrc, uint32_t trackSeed);
    void snapshot(uint32_t positionMs, LyricSnapshot& out);
    int activeLine(uint32_t positionMs);

private:
    static std::string sidecarPath(const std::string& audioPath);
    void publish(std::vector<LrcLine>&& lines, uint32_t trackSeed);

    std::vector<LrcLine> _sheets[2];
    uint32_t _seeds[2] = {0, 0};
    volatile int _active = 0;
    SemaphoreHandle_t _mutex;
};

extern LyricStore Lyrics;

}  // namespace lz
