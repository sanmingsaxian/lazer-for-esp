#include "strutil.h"
#include <cstdio>

namespace lz {

std::string trim(const std::string& value) {
    size_t b = 0, e = value.size();
    while (b < e && (unsigned char)value[b] <= ' ') ++b;
    while (e > b && (unsigned char)value[e - 1] <= ' ') --e;
    return value.substr(b, e - b);
}

std::string toLower(std::string value) {
    for (char& c : value)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return value;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

uint32_t fnv1a(const std::string& value) {
    uint32_t h = 2166136261u;
    for (unsigned char c : value) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

std::string urlEncode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 2);
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string urlDecode(const std::string& value) {
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < value.size()) {
            const int hi = hexVal(value[i + 1]);
            const int lo = hexVal(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += (char)((hi << 4) | lo);
                i += 2;
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

std::string jsonQuote(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    out += '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += (char)c;
        } else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            char escape[7];
            snprintf(escape, sizeof(escape), "\\u%04x", c);
            out += escape;
        } else {
            out += (char)c;
        }
    }
    out += '"';
    return out;
}

std::string u64str(uint64_t value) {
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
    return buffer;
}

}  // namespace lz
