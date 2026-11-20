#include "minitest.h"
#include "mp3info.h"
#include <cstring>

using namespace lz;

namespace {
// Build a valid MPEG1 Layer III header: 128 kbps, 44100 Hz, no padding.
void makeHeader(uint8_t b[4], int bitrateKbps = 128, int rateIdx = 0) {
    // bitrate index for MPEG1 L3: 128 -> 9
    static const int idx[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    int brIdx = 9;
    for (int i = 1; i < 15; ++i) {
        static const int rates[15] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
        if (rates[i] == bitrateKbps) { brIdx = i; break; }
    }
    (void)idx;
    b[0] = 0xff;
    b[1] = 0xfb;  // sync + MPEG1 + Layer III + no CRC
    b[2] = (uint8_t)((brIdx << 4) | (rateIdx << 2));
    b[3] = 0x00;
}
}  // namespace

CASE(parse_mpeg1_layer3_128k) {
    uint8_t b[4];
    makeHeader(b);
    Mp3Header h = parseMp3Header(b);
    CHECK(h.valid);
    CHECK_EQ(h.bitrate, 128000);
    CHECK_EQ(h.sampleRate, 44100);
    CHECK_EQ(h.version, 1);
    CHECK_EQ(h.layer, 3);
}

CASE(parse_rejects_garbage) {
    uint8_t b[4] = {0x00, 0x00, 0x00, 0x00};
    CHECK(!parseMp3Header(b).valid);
    uint8_t c[4] = {0xff, 0xff, 0xff, 0xff};  // invalid version/layer/bitrate
    CHECK(!parseMp3Header(c).valid);
}

CASE(estimate_bitrate_finds_first_frame) {
    uint8_t window[64];
    memset(window, 0, sizeof(window));
    makeHeader(window + 10, 192);
    CHECK_EQ(estimateBitrate(window, sizeof(window)), 192000u);
}

CASE(estimate_bitrate_default_when_no_frame) {
    uint8_t window[32];
    memset(window, 0, sizeof(window));
    CHECK_EQ(estimateBitrate(window, sizeof(window)), 128000u);
}

CASE(duration_matches_original_formula) {
    // 5 MB file, no tag: (5242880-128)*8/128000 = 327 seconds.
    CHECK_EQ(estimateDuration(5242880, 0, 128000), 327u);
    // Tag offset subtracts from audio bytes.
    CHECK_EQ(estimateDuration(5242880, 4096, 128000), 327u);
    // Small files: no 128-byte subtraction when audioBytes <= 128.
    CHECK_EQ(estimateDuration(100, 0, 128000), 0u);
    CHECK_EQ(estimateDuration(0, 0, 128000), 0u);
}

CASE(seek_byte_offset) {
    CHECK_EQ(seekByteOffset(60, 128000), 960000ull);
    CHECK_EQ(seekByteOffset(0, 128000), 0ull);
}

int main() { return test_summary(); }
