#include "pathutil.h"
#include "strutil.h"

namespace lz {

std::string baseName(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string parentPath(const std::string& path) {
    if (path == "/") return "/";
    const size_t slash = path.rfind('/');
    return slash == std::string::npos || slash == 0 ? "/" : path.substr(0, slash);
}

std::string joinPath(const std::string& parent, const std::string& name) {
    return parent == "/" ? "/" + name : parent + "/" + name;
}

bool normalizePath(const std::string& input, std::string& output) {
    if (input.empty() || input.size() > 180 || input[0] != '/') return false;
    output = "/";
    size_t cursor = 1;
    while (cursor <= input.size()) {
        size_t slash = input.find('/', cursor);
        if (slash == std::string::npos) slash = input.size();
        const std::string part = input.substr(cursor, slash - cursor);
        if (!part.empty() && part != ".") {
            if (part == "..") return false;
            for (unsigned char c : part)
                if (c < 0x20 || c == '\\') return false;
            if (output != "/") output += '/';
            output += part;
            if (output.size() > 180) return false;
        }
        if (slash == input.size()) break;
        cursor = slash + 1;
    }
    return true;
}

bool validChildName(const std::string& name) {
    if (name.empty() || name.size() > 80 || name == "." || name == "..") return false;
    static const char* forbidden = "\\/:*?\"<>|";
    for (unsigned char c : name) {
        if (c < 0x20) return false;
        for (const char* f = forbidden; *f; ++f)
            if (c == (unsigned char)*f) return false;
    }
    return true;
}

bool protectedSystemPath(const std::string& path) {
    const std::string lower = toLower(path);
    return lower == "/system volume information" ||
           startsWith(lower, "/system volume information/");
}

bool textFilePath(const std::string& path) {
    const std::string lower = toLower(path);
    static const char* extensions[] = {".lrc", ".txt", ".md", ".json",
                                       ".csv", ".log", ".m3u", ".m3u8"};
    for (const char* extension : extensions)
        if (endsWith(lower, extension)) return true;
    return false;
}

std::string contentTypeFor(const std::string& path) {
    const std::string lower = toLower(path);
    if (endsWith(lower, ".mp3")) return "audio/mpeg";
    if (endsWith(lower, ".jpg") || endsWith(lower, ".jpeg")) return "image/jpeg";
    if (endsWith(lower, ".png")) return "image/png";
    if (endsWith(lower, ".gif")) return "image/gif";
    if (endsWith(lower, ".json")) return "application/json";
    if (endsWith(lower, ".csv")) return "text/csv; charset=utf-8";
    if (textFilePath(lower)) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

}  // namespace lz
