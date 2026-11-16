#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <vector>

struct Track {
    String path;
    String title;
    String artist;
    String album;
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
    int findByPath(const String& path) const;

private:
    void scanDirectory(const String& path, uint8_t depth);
    bool addFile(File& file);
    void readMetadata(Track& track);
    void readID3v2(File& file, Track& track, uint32_t& audioOffset);
    void readID3v1(File& file, Track& track);
    void estimateDuration(File& file, Track& track, uint32_t audioOffset);
    static String cleanName(const String& path);
    static uint32_t hash(const String& value);

    bool _ready = false;
    std::vector<Track> _tracks;
};

extern MusicLibrary Library;
