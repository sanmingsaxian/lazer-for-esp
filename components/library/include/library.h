#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lz {

struct Track {
    std::string path;  // SD-root-relative, e.g. "/Music/a.mp3"
    std::string title;
    std::string artist;
    std::string album;
    uint32_t duration = 0;
    uint32_t fileSize = 0;
    uint32_t bitrate = 128000;
    uint32_t seed = 0;
};

class MusicLibrary {
public:
    static constexpr size_t MAX_TRACKS = 80;

    bool begin();
    bool rescan();
    void clear();

    bool ready() const { return _ready; }
    size_t count() const { return _tracks.size(); }
    const Track* track(size_t index) const;
    const std::vector<Track>& tracks() const { return _tracks; }
    int findByPath(const std::string& path) const;

private:
    void scanDirectory(const std::string& logicalPath, uint8_t depth);
    bool addFile(const std::string& logicalPath, uint32_t fileSize);
    void readMetadata(Track& track);
    static std::string cleanName(const std::string& path);

    bool _ready = false;
    std::vector<Track> _tracks;
};

extern MusicLibrary Library;

}  // namespace lz
