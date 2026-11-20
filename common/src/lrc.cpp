#include "lrc.h"
#include "strutil.h"
#include <algorithm>
#include <cstdlib>

namespace lz {

namespace {
// Parses "[mm:ss" or "[mm:ss.xx" (also [mm:ss.xxx]); returns start ms and the
// index just past the closing bracket, or false for metadata tags.
bool parseTimestamp(const std::string& line, size_t bracketStart, size_t& bracketEnd,
                    uint32_t& startMs) {
    size_t i = bracketStart + 1;
    size_t colon = line.find(':', i);
    if (colon == std::string::npos) return false;
    const std::string minutes = line.substr(i, colon - i);
    if (minutes.empty()) return false;
    for (char c : minutes)
        if (c < '0' || c > '9') return false;
    size_t j = colon + 1;
    size_t secEnd = j;
    while (secEnd < line.size() && line[secEnd] >= '0' && line[secEnd] <= '9') ++secEnd;
    if (secEnd == j) return false;
    uint32_t seconds = (uint32_t)strtoul(line.substr(j, secEnd - j).c_str(), nullptr, 10);
    uint32_t fraction = 0;
    size_t k = secEnd;
    if (k < line.size() && (line[k] == '.' || line[k] == ':')) {
        ++k;
        size_t fracStart = k;
        while (k < line.size() && line[k] >= '0' && line[k] <= '9') ++k;
        std::string frac = line.substr(fracStart, k - fracStart);
        if (frac.empty()) return false;
        while (frac.size() < 3) frac += '0';
        fraction = (uint32_t)strtoul(frac.substr(0, 3).c_str(), nullptr, 10);
    }
    if (k >= line.size() || line[k] != ']') return false;
    bracketEnd = k + 1;
    startMs = ((uint32_t)strtoul(minutes.c_str(), nullptr, 10) * 60 + seconds) * 1000 + fraction;
    return true;
}
}  // namespace

std::vector<LrcLine> parseLrc(const std::string& text, size_t maxLines) {
    std::vector<LrcLine> lines;
    size_t pos = 0;
    while (pos < text.size() && lines.size() < maxLines) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Collect every leading timestamp tag on this line.
        std::vector<uint32_t> stamps;
        size_t cursor = 0;
        while (cursor < line.size() && line[cursor] == '[') {
            size_t bracketEnd = 0;
            uint32_t startMs = 0;
            if (!parseTimestamp(line, cursor, bracketEnd, startMs)) break;
            stamps.push_back(startMs);
            cursor = bracketEnd;
        }
        if (stamps.empty()) continue;
        std::string payload = trim(line.substr(cursor));
        for (uint32_t stamp : stamps) {
            if (lines.size() >= maxLines) break;
            lines.push_back({stamp, payload});
        }
    }
    std::stable_sort(lines.begin(), lines.end(),
                     [](const LrcLine& a, const LrcLine& b) { return a.startMs < b.startMs; });
    return lines;
}

int activeLrcLine(const std::vector<LrcLine>& lines, uint32_t positionMs) {
    int active = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].startMs <= positionMs) active = (int)i;
        else break;
    }
    return active;
}

void lrcSnapshot(const std::vector<LrcLine>& lines, uint32_t positionMs, LrcView& out) {
    out = LrcView{};
    if (lines.empty()) return;
    out.available = true;
    out.activeLine = activeLrcLine(lines, positionMs);
    const int center = out.activeLine >= 0 ? out.activeLine : 0;
    for (size_t slot = 0; slot < LrcView::VISIBLE; ++slot) {
        const int relative = (int)slot - 3;
        const int index = center + relative;
        if (index < 0 || index >= (int)lines.size()) continue;
        LrcViewLine& view = out.lines[slot];
        view.valid = true;
        view.text = lines[index].text;
        view.startMs = lines[index].startMs;
        view.endMs = index + 1 < (int)lines.size() ? lines[index + 1].startMs
                                                   : lines[index].startMs;
        view.relative = relative;
        if (index == out.activeLine) out.active = (int)slot;
    }
}

}  // namespace lz
