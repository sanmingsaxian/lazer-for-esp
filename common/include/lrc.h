#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lz {

struct LrcLine {
    uint32_t startMs = 0;
    std::string text;
};

struct LrcViewLine {
    bool valid = false;
    std::string text;
    uint32_t startMs = 0;
    uint32_t endMs = 0;
    int relative = 0;
};

struct LrcView {
    static constexpr size_t VISIBLE = 7;
    LrcViewLine lines[VISIBLE];
    int active = -1;       // visible slot used for highlighting
    int activeLine = -1;   // absolute line index, -1 before first timestamp
    bool available = false;
};

// Parses [mm:ss(.xx)] timestamps (multiple per line allowed), ignores metadata
// tags such as [ar:...], sorts by time and caps at maxLines (72 on device).
std::vector<LrcLine> parseLrc(const std::string& text, size_t maxLines = 72);

int activeLrcLine(const std::vector<LrcLine>& lines, uint32_t positionMs);

// Seven-line window centered on the active line (relative -3..+3); the player
// renders relative -2..+2. endMs is the next line's start (== startMs for the
// last line, which the renderer treats as fully progressed).
void lrcSnapshot(const std::vector<LrcLine>& lines, uint32_t positionMs, LrcView& out);

}  // namespace lz
