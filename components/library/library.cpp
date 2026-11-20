#include "library.h"
#include "id3.h"
#include "pathutil.h"
#include "storage.h"
#include "strutil.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

namespace lz {

MusicLibrary Library;

namespace {
// FILE*-backed reader for the host-tested ID3/duration pipeline.
struct FileReader : IId3Reader {
    FILE* file = nullptr;
    uint32_t fileSize = 0;
    explicit FileReader(const std::string& vfsPath) {
        file = fopen(vfsPath.c_str(), "rb");
        if (file) {
            fseek(file, 0, SEEK_END);
            long end = ftell(file);
            fileSize = end > 0 ? (uint32_t)end : 0;
        }
    }
    ~FileReader() override {
        if (file) fclose(file);
    }
    uint32_t size() const override { return fileSize; }
    bool readAt(uint32_t offset, uint8_t* dst, uint32_t len) override {
        if (!file || offset + len > fileSize) return false;
        if (fseek(file, offset, SEEK_SET) != 0) return false;
        return fread(dst, 1, len, file) == len;
    }
};
}  // namespace

bool MusicLibrary::begin() {
    _ready = storage_mount();
    if (_ready) rescan();
    return _ready;
}

void MusicLibrary::clear() { _tracks.clear(); }

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

void MusicLibrary::scanDirectory(const std::string& logicalPath, uint8_t depth) {
    if (_tracks.size() >= MAX_TRACKS) return;
    const std::string vfsPath = storage_vfs_path(logicalPath);
    DIR* dir = opendir(vfsPath.c_str());
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr && _tracks.size() < MAX_TRACKS) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        const std::string childLogical = joinPath(logicalPath, name);
        const std::string childVfs = vfsPath + "/" + name;
        struct stat info;
        if (stat(childVfs.c_str(), &info) != 0) continue;
        if (S_ISDIR(info.st_mode)) {
            if (depth && name[0] != '.' && name != "System Volume Information")
                scanDirectory(childLogical, depth - 1);
        } else if (S_ISREG(info.st_mode)) {
            addFile(childLogical, (uint32_t)info.st_size);
        }
    }
    closedir(dir);
}

bool MusicLibrary::addFile(const std::string& logicalPath, uint32_t fileSize) {
    if (!endsWith(toLower(logicalPath), ".mp3")) return false;
    Track t;
    t.path = logicalPath;
    t.fileSize = fileSize;
    t.title = cleanName(logicalPath);
    const size_t split = t.title.find(" - ");
    if (split != std::string::npos && split > 0) {
        t.artist = t.title.substr(0, split);
        t.title = t.title.substr(split + 3);
    } else {
        t.artist = "本地音乐";
    }
    t.album = "Cardputer";
    t.seed = fnv1a(logicalPath);
    readMetadata(t);
    _tracks.push_back(t);
    return true;
}

const Track* MusicLibrary::track(size_t index) const {
    return index < _tracks.size() ? &_tracks[index] : nullptr;
}

int MusicLibrary::findByPath(const std::string& path) const {
    for (size_t i = 0; i < _tracks.size(); ++i)
        if (_tracks[i].path == path) return (int)i;
    return -1;
}

std::string MusicLibrary::cleanName(const std::string& path) {
    const size_t slash = path.rfind('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return name;
}

void MusicLibrary::readMetadata(Track& track) {
    FileReader reader(storage_vfs_path(track.path));
    if (!reader.file) return;
    Id3Tags tags;
    tags.title = track.title;
    tags.artist = track.artist;
    tags.album = track.album;
    readId3Tags(reader, tags);
    // Original keeps filename-derived title when tags provide nothing usable.
    if (!tags.title.empty()) track.title = tags.title;
    track.artist = tags.artist;
    track.album = tags.album;
    track.bitrate = tags.bitrate;
    track.duration = tags.duration;
}

}  // namespace lz
