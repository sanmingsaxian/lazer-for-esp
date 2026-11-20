#pragma once
#include <cstddef>
#include <cstdint>

namespace lz {

struct Mp3Header {
    bool valid = false;
    int bitrate = 0;      // bits per second
    int sampleRate = 0;   // Hz
    int version = 0;      // 1 = MPEG1, 2 = MPEG2, 25 = MPEG2.5
    int layer = 0;        // 1..3
    bool padding = false;
};

Mp3Header parseMp3Header(const uint8_t bytes[4]);

// Port of MusicLibrary::estimateDuration bitrate scan: first MPEG1 Layer III
// frame with a non-zero bitrate inside the window wins; default 128000.
uint32_t estimateBitrate(const uint8_t* data, size_t len);

// audioBytes = fileSize - audioOffset, minus 128 (ID3v1 allowance) when >128.
uint32_t estimateDuration(uint32_t fileSize, uint32_t audioOffset, uint32_t bitrate);

// CBR byte position used for seeking (seconds * bitrate / 8).
uint64_t seekByteOffset(uint32_t seconds, uint32_t bitrate);

}  // namespace lz
