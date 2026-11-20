#include "mp3info.h"

namespace lz {

namespace {
// MPEG1 Layer III bitrate table (kbps), index 0 and 15 invalid.
const int kRatesMpeg1L3[16] = {0, 32, 40, 48, 56, 64, 80, 96,
                               112, 128, 160, 192, 224, 256, 320, 0};
const int kRatesMpeg2L3[16] = {0, 8, 16, 24, 32, 40, 48, 56,
                               64, 80, 96, 112, 128, 144, 160, 0};
const int kSampleRates[4][3] = {
    {44100, 48000, 32000},  // MPEG1
    {22050, 24000, 16000},  // MPEG2
    {11025, 12000, 8000},   // MPEG2.5
    {0, 0, 0},
};
}  // namespace

Mp3Header parseMp3Header(const uint8_t b[4]) {
    Mp3Header h;
    if (b[0] != 0xff || (b[1] & 0xe0) != 0xe0) return h;
    const int versionBits = (b[1] >> 3) & 3;
    const int layerBits = (b[1] >> 1) & 3;
    if (versionBits == 1 || layerBits == 0) return h;
    const int rateIndex = (b[2] >> 4) & 15;
    const int srIndex = (b[2] >> 2) & 3;
    if (rateIndex == 0 || rateIndex == 15 || srIndex == 3) return h;
    h.version = versionBits == 3 ? 1 : (versionBits == 2 ? 2 : 25);
    h.layer = 4 - layerBits;
    const bool mpeg1 = h.version == 1;
    if (h.layer == 3)
        h.bitrate = (mpeg1 ? kRatesMpeg1L3[rateIndex] : kRatesMpeg2L3[rateIndex]) * 1000;
    else
        return h;  // this project only decodes Layer III
    h.sampleRate = kSampleRates[mpeg1 ? 0 : (h.version == 2 ? 1 : 2)][srIndex];
    h.padding = (b[2] >> 1) & 1;
    h.valid = h.bitrate > 0 && h.sampleRate > 0;
    return h;
}

uint32_t estimateBitrate(const uint8_t* data, size_t len) {
    // Mirrors MusicLibrary::estimateDuration: only MPEG1 Layer III frames with
    // a non-zero bitrate from the original 16-entry table are accepted.
    for (size_t pos = 0; pos + 4 <= len; ++pos) {
        const uint8_t* b = data + pos;
        if (b[0] != 0xff || (b[1] & 0xe0) != 0xe0) continue;
        const uint8_t version = (b[1] >> 3) & 3;
        const uint8_t layer = (b[1] >> 1) & 3;
        const uint8_t rateIndex = (b[2] >> 4) & 15;
        if (version == 3 && layer == 1 && kRatesMpeg1L3[rateIndex])
            return (uint32_t)kRatesMpeg1L3[rateIndex] * 1000;
    }
    return 128000;
}

uint32_t estimateDuration(uint32_t fileSize, uint32_t audioOffset, uint32_t bitrate) {
    uint32_t audioBytes = fileSize > audioOffset ? fileSize - audioOffset : 0;
    if (audioBytes > 128) audioBytes -= 128;
    return bitrate ? (uint32_t)(((uint64_t)audioBytes * 8) / bitrate) : 0;
}

uint64_t seekByteOffset(uint32_t seconds, uint32_t bitrate) {
    return ((uint64_t)seconds * bitrate) / 8;
}

}  // namespace lz
