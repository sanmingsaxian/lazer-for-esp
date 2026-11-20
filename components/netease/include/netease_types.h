#pragma once
#include <cstdint>
#include <string>

namespace lz {

struct OnlineTrack {
    uint64_t id = 0;
    std::string title;
    std::string artist;
    std::string album;
    uint32_t duration = 0;
};

struct OnlinePlayback {
    std::string url;
    std::string lyric;
    std::string format;
    uint32_t bitrate = 0;
    uint64_t size = 0;
};

}  // namespace lz
