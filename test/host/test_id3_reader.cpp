#include "minitest.h"
#include "id3.h"
#include <cstring>
#include <vector>

using namespace lz;

namespace {
struct MemReader : IId3Reader {
    std::vector<uint8_t> data;
    uint32_t size() const override { return (uint32_t)data.size(); }
    bool readAt(uint32_t offset, uint8_t* dst, uint32_t len) override {
        if (offset + len > data.size()) return false;
        memcpy(dst, data.data() + offset, len);
        return true;
    }
};
void putSync(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((x >> 21) & 0x7f); v.push_back((x >> 14) & 0x7f);
    v.push_back((x >> 7) & 0x7f); v.push_back(x & 0x7f);
}
void put32be(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back((x >> 16) & 0xff);
    v.push_back((x >> 8) & 0xff); v.push_back(x & 0xff);
}
void addFrame(std::vector<uint8_t>& tag, const char* id, const char* text) {
    size_t len = strlen(text);
    for (int i = 0; i < 4; ++i) tag.push_back(id[i]);
    put32be(tag, (uint32_t)len + 1);
    tag.push_back(0); tag.push_back(0);
    tag.push_back(0);  // latin1
    tag.insert(tag.end(), text, text + len);
}
void addBigFrame(std::vector<uint8_t>& tag, const char* id, size_t len) {
    for (int i = 0; i < 4; ++i) tag.push_back(id[i]);
    put32be(tag, (uint32_t)len);
    tag.push_back(0); tag.push_back(0);
    tag.insert(tag.end(), len, 0x22);
}
// MPEG1 Layer III, 128 kbps, 44100 Hz.
void addFrameHeader(std::vector<uint8_t>& v) {
    v.push_back(0xff); v.push_back(0xfb); v.push_back(0x90); v.push_back(0x00);
}
}  // namespace

CASE(full_pipeline_v2_tag_then_audio) {
    std::vector<uint8_t> tag;
    addFrame(tag, "TIT2", "RealTitle");
    addBigFrame(tag, "APIC", 6000);  // must be skipped, not abort the walk
    addFrame(tag, "TPE1", "RealArtist");
    std::vector<uint8_t> file;
    file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
    putSync(file, (uint32_t)tag.size());
    file.insert(file.end(), tag.begin(), tag.end());
    uint32_t audioOffset = (uint32_t)file.size();
    addFrameHeader(file);
    file.insert(file.end(), 100000, 0);  // audio payload
    MemReader r; r.data = file;
    Id3Tags tags;
    tags.title = "Fallback"; tags.artist = "本地音乐"; tags.album = "Cardputer";
    readId3Tags(r, tags);
    CHECK_EQ(tags.title, std::string("RealTitle"));
    CHECK_EQ(tags.artist, std::string("RealArtist"));
    CHECK_EQ(tags.audioOffset, audioOffset);
    CHECK_EQ(tags.bitrate, 128000u);
    // duration = (fileSize - audioOffset - 128) * 8 / 128000
    uint32_t expect = ((uint32_t)file.size() - audioOffset - 128) * 8 / 128000;
    CHECK_EQ(tags.duration, expect);
}

CASE(no_v2_falls_back_to_v1_and_scans_from_zero) {
    std::vector<uint8_t> file;
    addFrameHeader(file);
    file.insert(file.end(), 50000, 0);
    uint8_t v1[128];
    memset(v1, 0, sizeof(v1));
    memcpy(v1, "TAG", 3);
    memcpy(v1 + 3, "V1Title", 7);
    memcpy(v1 + 33, "V1Artist", 8);
    file.insert(file.end(), v1, v1 + 128);
    MemReader r; r.data = file;
    Id3Tags tags;
    tags.title = ""; tags.artist = "本地音乐"; tags.album = "Cardputer";
    readId3Tags(r, tags);
    CHECK_EQ(tags.title, std::string("V1Title"));
    CHECK_EQ(tags.artist, std::string("V1Artist"));
    CHECK_EQ(tags.audioOffset, 0u);
    CHECK_EQ(tags.bitrate, 128000u);
}

CASE(v2_present_but_empty_fields_keep_filename_defaults) {
    std::vector<uint8_t> file;
    file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
    putSync(file, 0);  // empty tag body
    addFrameHeader(file);
    file.insert(file.end(), 20000, 0);
    MemReader r; r.data = file;
    Id3Tags tags;
    tags.title = "FromFile"; tags.artist = "本地音乐"; tags.album = "Cardputer";
    readId3Tags(r, tags);
    CHECK_EQ(tags.title, std::string("FromFile"));
    CHECK_EQ(tags.artist, std::string("本地音乐"));
    CHECK_EQ(tags.audioOffset, 10u);
}

int main() { return test_summary(); }
