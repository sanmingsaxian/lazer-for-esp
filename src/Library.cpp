#include "Library.h"
#include <algorithm>
#include <cstring>

MusicLibrary Library;

namespace {
constexpr int SD_SCK = 40;
constexpr int SD_MISO = 39;
constexpr int SD_MOSI = 14;
constexpr int SD_CS = 12;

uint32_t synchsafe(const uint8_t* p) {
    return ((uint32_t)(p[0] & 0x7f) << 21) | ((uint32_t)(p[1] & 0x7f) << 14) |
           ((uint32_t)(p[2] & 0x7f) << 7) | (uint32_t)(p[3] & 0x7f);
}

void appendUtf8(String& out, uint32_t cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xc0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out += (char)(0xe0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3f));
        out += (char)(0x80 | (cp & 0x3f));
    }
}

String decodeText(uint8_t encoding, const uint8_t* data, size_t size) {
    String out;
    if (!size) return out;
    if (encoding == 0 || encoding == 3) {
        for (size_t i = 0; i < size && data[i]; ++i) out += (char)data[i];
    } else if (encoding == 1 || encoding == 2) {
        const bool be = encoding == 2 || (size >= 2 && data[0] == 0xfe && data[1] == 0xff);
        size_t i = (encoding == 1 && size >= 2) ? 2 : 0;
        for (; i + 1 < size; i += 2) {
            uint16_t cp = be ? ((uint16_t)data[i] << 8) | data[i + 1]
                             : ((uint16_t)data[i + 1] << 8) | data[i];
            if (!cp) break;
            appendUtf8(out, cp);
        }
    }
    out.trim();
    return out;
}
}  // namespace

bool MusicLibrary::begin() {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    _ready = SD.begin(SD_CS, SPI, 25000000);
    if (_ready) rescan();
    return _ready;
}

void MusicLibrary::clear() {
    _tracks.clear();
}

bool MusicLibrary::rescan() {
    if (!_ready) return false;
    clear();
    _tracks.reserve(32);
    scanDirectory("/", 2);
    std::sort(_tracks.begin(), _tracks.end(), [](const Track& a, const Track& b) {
        return strcasecmp(a.title.c_str(), b.title.c_str()) < 0;
    });
    return true;
}

void MusicLibrary::scanDirectory(const String& path, uint8_t depth) {
    if (_tracks.size() >= MAX_TRACKS) return;
    File root = SD.open(path);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }
    File file = root.openNextFile();
    while (file && _tracks.size() < MAX_TRACKS) {
        if (file.isDirectory()) {
            String name = file.name();
            if (depth && !name.startsWith(".") && name != "System Volume Information") {
                scanDirectory(file.path(), depth - 1);
            }
        } else {
            addFile(file);
        }
        file.close();
        file = root.openNextFile();
    }
    if (file) file.close();
    root.close();
}

bool MusicLibrary::addFile(File& file) {
    String path = file.path();
    String lower = path;
    lower.toLowerCase();
    if (!lower.endsWith(".mp3")) return false;
    Track t;
    t.path = path;
    t.fileSize = file.size();
    t.title = cleanName(path);
    int split = t.title.indexOf(" - ");
    if (split > 0) {
        t.artist = t.title.substring(0, split);
        t.title = t.title.substring(split + 3);
    } else {
        t.artist = "本地音乐";
    }
    t.album = "Cardputer";
    t.seed = hash(path);
    readMetadata(t);
    _tracks.push_back(t);
    return true;
}

const Track* MusicLibrary::track(size_t index) const {
    return index < _tracks.size() ? &_tracks[index] : nullptr;
}

int MusicLibrary::findByPath(const String& path) const {
    for (size_t i = 0; i < _tracks.size(); ++i) {
        if (_tracks[i].path == path) return (int)i;
    }
    return -1;
}

String MusicLibrary::cleanName(const String& path) {
    int slash = path.lastIndexOf('/');
    String name = slash >= 0 ? path.substring(slash + 1) : path;
    int dot = name.lastIndexOf('.');
    if (dot > 0) name = name.substring(0, dot);
    return name;
}

uint32_t MusicLibrary::hash(const String& value) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < value.length(); ++i) {
        h ^= (uint8_t)value[i];
        h *= 16777619u;
    }
    return h;
}

void MusicLibrary::readMetadata(Track& track) {
    File file = SD.open(track.path, FILE_READ);
    if (!file) return;
    uint32_t audioOffset = 0;
    readID3v2(file, track, audioOffset);
    if (track.title.length() == 0 || track.artist == "本地音乐") readID3v1(file, track);
    estimateDuration(file, track, audioOffset);
    file.close();
}

void MusicLibrary::readID3v2(File& file, Track& track, uint32_t& audioOffset) {
    uint8_t h[10];
    file.seek(0);
    if (file.read(h, 10) != 10 || memcmp(h, "ID3", 3) != 0) {
        audioOffset = 0;
        return;
    }
    const uint8_t version = h[3];
    const uint32_t tagSize = synchsafe(h + 6);
    audioOffset = 10 + tagSize;
    uint32_t pos = 10;
    const uint32_t end = min(audioOffset, file.size());
    while (pos + 10 <= end) {
        file.seek(pos);
        uint8_t fh[10];
        if (file.read(fh, 10) != 10 || fh[0] == 0) break;
        uint32_t size = version == 4 ? synchsafe(fh + 4)
                                     : ((uint32_t)fh[4] << 24) | ((uint32_t)fh[5] << 16) |
                                           ((uint32_t)fh[6] << 8) | fh[7];
        if (!size || pos + 10 + size > end) break;
        const bool wanted = memcmp(fh, "TIT2", 4) == 0 || memcmp(fh, "TPE1", 4) == 0 ||
                            memcmp(fh, "TALB", 4) == 0;
        // Skip APIC and other large frames instead of abandoning all text tags.
        if (wanted && size > 1 && size <= 4096) {
            uint8_t* buffer = (uint8_t*)malloc(size);
            if (buffer) {
                if (file.read(buffer, size) == size) {
                    String text = decodeText(buffer[0], buffer + 1, size - 1);
                    if (text.length()) {
                        if (memcmp(fh, "TIT2", 4) == 0) track.title = text;
                        else if (memcmp(fh, "TPE1", 4) == 0) track.artist = text;
                        else track.album = text;
                    }
                }
                free(buffer);
            }
        }
        pos += 10 + size;
    }
}

void MusicLibrary::readID3v1(File& file, Track& track) {
    if (file.size() < 128) return;
    file.seek(file.size() - 128);
    uint8_t tag[128];
    if (file.read(tag, 128) != 128 || memcmp(tag, "TAG", 3) != 0) return;
    auto field = [](const uint8_t* p, size_t n) {
        String value;
        for (size_t i = 0; i < n && p[i]; ++i) value += (char)p[i];
        value.trim();
        return value;
    };
    if (track.title.length() == 0) track.title = field(tag + 3, 30);
    if (track.artist == "本地音乐") track.artist = field(tag + 33, 30);
    if (track.album == "Cardputer") track.album = field(tag + 63, 30);
}

void MusicLibrary::estimateDuration(File& file, Track& track, uint32_t audioOffset) {
    static const uint16_t rates[16] = {0, 32, 40, 48, 56, 64, 80, 96,
                                      112, 128, 160, 192, 224, 256, 320, 0};
    track.bitrate = 128000;
    const uint32_t limit = min(file.size(), audioOffset + 65536u);
    for (uint32_t pos = audioOffset; pos + 4 < limit; ++pos) {
        file.seek(pos);
        uint8_t b[4];
        if (file.read(b, 4) != 4) break;
        if (b[0] != 0xff || (b[1] & 0xe0) != 0xe0) continue;
        const uint8_t version = (b[1] >> 3) & 3;
        const uint8_t layer = (b[1] >> 1) & 3;
        const uint8_t rateIndex = (b[2] >> 4) & 15;
        if (version == 3 && layer == 1 && rates[rateIndex]) {
            track.bitrate = (uint32_t)rates[rateIndex] * 1000;
            break;
        }
    }
    uint32_t audioBytes = file.size() > audioOffset ? file.size() - audioOffset : 0;
    if (audioBytes > 128) audioBytes -= 128;
    track.duration = track.bitrate ? (uint32_t)(((uint64_t)audioBytes * 8) / track.bitrate) : 0;
}
