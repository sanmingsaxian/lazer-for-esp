#include "id3.h"
#include "mp3info.h"
#include "strutil.h"
#include <cstdlib>
#include <cstring>

namespace lz {

uint32_t synchsafe(const uint8_t* p) {
    return ((uint32_t)(p[0] & 0x7f) << 21) | ((uint32_t)(p[1] & 0x7f) << 14) |
           ((uint32_t)(p[2] & 0x7f) << 7) | (uint32_t)(p[3] & 0x7f);
}

namespace {
void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xc0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out += (char)(0xe0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3f));
        out += (char)(0x80 | (cp & 0x3f));
    } else if (cp < 0x110000) {
        out += (char)(0xf0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3f));
        out += (char)(0x80 | ((cp >> 6) & 0x3f));
        out += (char)(0x80 | (cp & 0x3f));
    }
}
}  // namespace

std::string decodeId3Text(uint8_t encoding, const uint8_t* data, size_t size) {
    std::string out;
    if (!size) return out;
    if (encoding == 0 || encoding == 3) {
        for (size_t i = 0; i < size && data[i]; ++i) out += (char)data[i];
    } else if (encoding == 1 || encoding == 2) {
        const bool be = encoding == 2 || (size >= 2 && data[0] == 0xfe && data[1] == 0xff);
        size_t i = (encoding == 1 && size >= 2) ? 2 : 0;
        for (; i + 1 < size; i += 2) {
            uint16_t unit = be ? ((uint16_t)data[i] << 8) | data[i + 1]
                               : ((uint16_t)data[i + 1] << 8) | data[i];
            if (!unit) break;
            uint32_t cp = unit;
            if (unit >= 0xd800 && unit <= 0xdbff && i + 3 < size) {
                const uint16_t low = be ? ((uint16_t)data[i + 2] << 8) | data[i + 3]
                                        : ((uint16_t)data[i + 3] << 8) | data[i + 2];
                if (low >= 0xdc00 && low <= 0xdfff) {
                    cp = 0x10000 + ((uint32_t)(unit - 0xd800) << 10) + (low - 0xdc00);
                    i += 2;
                }
            }
            appendUtf8(out, cp);
        }
    }
    return trim(out);
}

void parseId3v2Frames(const uint8_t* body, size_t bodyLen, uint8_t version, Id3Tags& tags) {
    size_t pos = 0;
    while (pos + 10 <= bodyLen) {
        const uint8_t* fh = body + pos;
        if (fh[0] == 0) break;
        const uint32_t size = version == 4
            ? synchsafe(fh + 4)
            : ((uint32_t)fh[4] << 24) | ((uint32_t)fh[5] << 16) | ((uint32_t)fh[6] << 8) | fh[7];
        if (!size || pos + 10 + size > bodyLen) break;
        const bool wanted = memcmp(fh, "TIT2", 4) == 0 || memcmp(fh, "TPE1", 4) == 0 ||
                            memcmp(fh, "TALB", 4) == 0;
        // Skip APIC and other large frames instead of abandoning all text tags.
        if (wanted && size > 1 && size <= 4096) {
            const std::string text = decodeId3Text(fh[10], fh + 11, size - 1);
            if (!text.empty()) {
                if (memcmp(fh, "TIT2", 4) == 0) tags.title = text;
                else if (memcmp(fh, "TPE1", 4) == 0) tags.artist = text;
                else tags.album = text;
            }
        }
        pos += 10 + size;
    }
}

void parseId3v1(const uint8_t tag[128], Id3Tags& tags) {
    if (memcmp(tag, "TAG", 3) != 0) return;
    auto field = [](const uint8_t* p, size_t n) {
        std::string value;
        for (size_t i = 0; i < n && p[i]; ++i) value += (char)p[i];
        return trim(value);
    };
    if (tags.title.empty()) tags.title = field(tag + 3, 30);
    if (tags.artist == "本地音乐") tags.artist = field(tag + 33, 30);
    if (tags.album == "Cardputer") tags.album = field(tag + 63, 30);
}

void readId3Tags(IId3Reader& reader, Id3Tags& tags) {
    const uint32_t fileSize = reader.size();
    tags.audioOffset = 0;
    uint8_t header[10];
    if (reader.readAt(0, header, 10) && memcmp(header, "ID3", 3) == 0) {
        const uint8_t version = header[3];
        const uint32_t tagSize = synchsafe(header + 6);
        tags.audioOffset = 10 + tagSize;
        const uint32_t end = tags.audioOffset < fileSize ? tags.audioOffset : fileSize;
        uint32_t pos = 10;
        uint8_t frameHeader[10];
        uint8_t frameBuffer[4096];
        while (pos + 10 <= end) {
            if (!reader.readAt(pos, frameHeader, 10) || frameHeader[0] == 0) break;
            const uint32_t size = version == 4
                ? synchsafe(frameHeader + 4)
                : ((uint32_t)frameHeader[4] << 24) | ((uint32_t)frameHeader[5] << 16) |
                      ((uint32_t)frameHeader[6] << 8) | frameHeader[7];
            if (!size || pos + 10 + size > end) break;
            const bool wanted = memcmp(frameHeader, "TIT2", 4) == 0 ||
                                memcmp(frameHeader, "TPE1", 4) == 0 ||
                                memcmp(frameHeader, "TALB", 4) == 0;
            if (wanted && size > 1 && size <= sizeof(frameBuffer)) {
                if (reader.readAt(pos + 10, frameBuffer, size)) {
                    const std::string text = decodeId3Text(frameBuffer[0], frameBuffer + 1, size - 1);
                    if (!text.empty()) {
                        if (memcmp(frameHeader, "TIT2", 4) == 0) tags.title = text;
                        else if (memcmp(frameHeader, "TPE1", 4) == 0) tags.artist = text;
                        else tags.album = text;
                    }
                }
            }
            pos += 10 + size;
        }
    }
    if (tags.title.empty() || tags.artist == "本地音乐") {
        if (fileSize >= 128) {
            uint8_t v1[128];
            if (reader.readAt(fileSize - 128, v1, 128)) parseId3v1(v1, tags);
        }
    }
    // Bitrate scan window: first MPEG1 Layer III frame within 64 KiB of audio.
    tags.bitrate = 128000;
    const uint32_t limit = fileSize < tags.audioOffset + 65536u ? fileSize
                                                                : tags.audioOffset + 65536u;
    if (limit > tags.audioOffset) {
        const uint32_t window = limit - tags.audioOffset;
        uint8_t* buffer = (uint8_t*)malloc(window);
        if (buffer) {
            if (reader.readAt(tags.audioOffset, buffer, window))
                tags.bitrate = estimateBitrate(buffer, window);
            free(buffer);
        }
    }
    tags.duration = estimateDuration(fileSize, tags.audioOffset, tags.bitrate);
}

}  // namespace lz
