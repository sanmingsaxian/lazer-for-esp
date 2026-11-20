#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace lz {

struct Id3Tags {
    std::string title;
    std::string artist;
    std::string album;
    uint32_t audioOffset = 0;
    uint32_t bitrate = 128000;
    uint32_t duration = 0;
};

uint32_t synchsafe(const uint8_t p[4]);

// ID3v2 text frame decoding: enc 0/3 latin1-ish, 1 UTF-16 with BOM, 2 UTF-16BE.
std::string decodeId3Text(uint8_t encoding, const uint8_t* data, size_t size);

// Parse TIT2/TPE1/TALB frames from the tag body (bytes after the 10-byte
// header). Frames larger than 4096 bytes (e.g. APIC) are skipped, not fatal.
void parseId3v2Frames(const uint8_t* body, size_t bodyLen, uint8_t version, Id3Tags& tags);

// Fills only fields still at their filename-derived defaults:
// title empty, artist "本地音乐", album "Cardputer".
void parseId3v1(const uint8_t tag[128], Id3Tags& tags);

// Byte-reader abstraction so the full tag/duration pipeline is host-testable.
struct IId3Reader {
    virtual ~IId3Reader() = default;
    virtual uint32_t size() const = 0;
    virtual bool readAt(uint32_t offset, uint8_t* dst, uint32_t len) = 0;
};

// Full port of MusicLibrary::readMetadata: ID3v2 frame walk (skipping large
// frames), ID3v1 fallback for default fields, bitrate scan window (64 KiB) and
// CBR duration estimate. tags must start with filename-derived defaults.
void readId3Tags(IId3Reader& reader, Id3Tags& tags);

}  // namespace lz
