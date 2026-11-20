#include "minitest.h"
#include "id3.h"
#include <cstring>
#include <vector>

using namespace lz;

namespace {
void put32be(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back((x >> 16) & 0xff);
    v.push_back((x >> 8) & 0xff); v.push_back(x & 0xff);
}
void putSync(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((x >> 21) & 0x7f); v.push_back((x >> 14) & 0x7f);
    v.push_back((x >> 7) & 0x7f); v.push_back(x & 0x7f);
}
// One text frame: id + size + 2 flag bytes + encoding byte + payload.
void addFrame(std::vector<uint8_t>& body, const char* id, uint8_t enc,
              const std::vector<uint8_t>& payload, bool v4 = false) {
    for (int i = 0; i < 4; ++i) body.push_back(id[i]);
    uint32_t size = (uint32_t)payload.size() + 1;
    if (v4) putSync(body, size); else put32be(body, size);
    body.push_back(0); body.push_back(0);
    body.push_back(enc);
    body.insert(body.end(), payload.begin(), payload.end());
}
std::vector<uint8_t> latin(const char* s) {
    return std::vector<uint8_t>(s, s + strlen(s));
}
}  // namespace

CASE(synchsafe_decode) {
    uint8_t p[4] = {0x00, 0x00, 0x02, 0x01};
    CHECK_EQ(synchsafe(p), 257u);
    uint8_t q[4] = {0x7f, 0x7f, 0x7f, 0x7f};
    CHECK_EQ(synchsafe(q), 0x0fffffffu);
}

CASE(decode_latin1_and_utf8) {
    auto data = latin("Hello ");
    CHECK_EQ(decodeId3Text(0, data.data(), data.size()), std::string("Hello"));
    CHECK_EQ(decodeId3Text(3, data.data(), data.size()), std::string("Hello"));
}

CASE(decode_utf16_with_bom) {
    // UTF-16LE BOM + "AB"
    uint8_t le[] = {0xff, 0xfe, 0x41, 0x00, 0x42, 0x00};
    CHECK_EQ(decodeId3Text(1, le, sizeof(le)), std::string("AB"));
    // UTF-16BE BOM + "AB"
    uint8_t be[] = {0xfe, 0xff, 0x00, 0x41, 0x00, 0x42};
    CHECK_EQ(decodeId3Text(1, be, sizeof(be)), std::string("AB"));
    // encoding 2 = UTF-16BE without BOM
    uint8_t be2[] = {0x00, 0x41, 0x00, 0x42};
    CHECK_EQ(decodeId3Text(2, be2, sizeof(be2)), std::string("AB"));
    // Non-BMP char (U+1F600) via surrogate pair -> 4-byte UTF-8.
    uint8_t sp[] = {0xff, 0xfe, 0x3d, 0xd8, 0x00, 0xde};
    CHECK_EQ(decodeId3Text(1, sp, sizeof(sp)), std::string("\xf0\x9f\x98\x80"));
}

CASE(v2_frames_fill_tags_and_skip_apic) {
    std::vector<uint8_t> body;
    addFrame(body, "TIT2", 0, latin("Title"));
    // Large APIC frame must be skipped without aborting later text frames.
    std::vector<uint8_t> big(5000, 0x11);
    addFrame(body, "APIC", 0, big);
    addFrame(body, "TPE1", 0, latin("Artist"));
    addFrame(body, "TALB", 0, latin("Album"));
    Id3Tags tags;
    parseId3v2Frames(body.data(), body.size(), 3, tags);
    CHECK_EQ(tags.title, std::string("Title"));
    CHECK_EQ(tags.artist, std::string("Artist"));
    CHECK_EQ(tags.album, std::string("Album"));
}

CASE(v2_v4_sizes_are_synchsafe) {
    std::vector<uint8_t> body;
    addFrame(body, "TIT2", 3, latin("V4Title"), true);
    Id3Tags tags;
    parseId3v2Frames(body.data(), body.size(), 4, tags);
    CHECK_EQ(tags.title, std::string("V4Title"));
}

CASE(v1_fills_only_defaults) {
    uint8_t tag[128];
    memset(tag, 0, sizeof(tag));
    memcpy(tag, "TAG", 3);
    memcpy(tag + 3, "V1Title", 7);
    memcpy(tag + 33, "V1Artist", 8);
    memcpy(tag + 63, "V1Album", 7);
    Id3Tags tags;
    tags.title = "Existing";
    tags.artist = "本地音乐";
    tags.album = "Cardputer";
    parseId3v1(tag, tags);
    CHECK_EQ(tags.title, std::string("Existing"));   // kept
    CHECK_EQ(tags.artist, std::string("V1Artist"));  // default replaced
    CHECK_EQ(tags.album, std::string("V1Album"));    // default replaced
}

int main() { return test_summary(); }
