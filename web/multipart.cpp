#define _GNU_SOURCE
#include "multipart.h"

#include <cstring>

namespace lz {

namespace {
std::string headerValue(const std::string& headers, const char* name) {
    const std::string key = std::string(name) + ":";
    size_t pos = 0;
    while (pos < headers.size()) {
        size_t eol = headers.find("\r\n", pos);
        if (eol == std::string::npos) eol = headers.size();
        const std::string line = headers.substr(pos, eol - pos);
        if (strncasecmp(line.c_str(), key.c_str(), key.size()) == 0) {
            size_t start = key.size();
            while (start < line.size() && line[start] == ' ') ++start;
            return line.substr(start);
        }
        pos = eol + 2;
    }
    return "";
}

// Extract name="..." / filename="..." from Content-Disposition.
std::string dispositionParam(const std::string& disposition, const char* param) {
    const std::string key = std::string(param) + "=\"";
    const size_t start = disposition.find(key);
    if (start == std::string::npos) return "";
    const size_t valueStart = start + key.size();
    std::string out;
    for (size_t i = valueStart; i < disposition.size(); ++i) {
        const char c = disposition[i];
        if (c == '\\' && i + 1 < disposition.size()) {
            out += disposition[++i];
            continue;
        }
        if (c == '"') break;
        out += c;
    }
    return out;
}
}  // namespace

bool MultipartReader::begin(httpd_req_t* req) {
    _req = req;
    char contentType[160] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", contentType, sizeof(contentType)) != ESP_OK)
        return false;
    const char* boundary = strcasestr(contentType, "boundary=");
    if (!boundary) return false;
    boundary += 9;
    std::string value = boundary;
    if (!value.empty() && value.front() == '"') {
        const size_t end = value.find('"', 1);
        value = end == std::string::npos ? value.substr(1) : value.substr(1, end - 1);
    } else {
        const size_t semi = value.find(';');
        if (semi != std::string::npos) value = value.substr(0, semi);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\r')) value.pop_back();
    if (value.empty()) return false;
    _delimiter = "\r\n--" + value;

    // Skip the preamble up to the first boundary line.
    const std::string first = "--" + value;
    for (;;) {
        const size_t at = _buf.find(first, _pos);
        if (at != std::string::npos) {
            _pos = at + first.size();
            // Boundary line ends with CRLF (or "--" for an empty form).
            if (_buf.compare(_pos, 2, "--") == 0) {
                _formEnded = true;
                _valid = true;
                return true;
            }
            if (_buf.compare(_pos, 2, "\r\n") == 0) _pos += 2;
            break;
        }
        if (fill() <= 0) return false;
    }
    _valid = parseHeaders();
    return _valid;
}

int MultipartReader::fill() {
    if (_eof) return 0;
    char chunk[2048];
    const int received = httpd_req_recv(_req, chunk, sizeof(chunk));
    if (received <= 0) {
        _eof = true;
        return 0;
    }
    // Drop already-consumed bytes to keep the buffer bounded.
    if (_pos > 0) {
        _buf.erase(0, _pos);
        _pos = 0;
    }
    _buf.append(chunk, received);
    return received;
}

bool MultipartReader::parseHeaders() {
    _filename.clear();
    _fieldName.clear();
    for (;;) {
        const size_t end = _buf.find("\r\n\r\n", _pos);
        if (end != std::string::npos) {
            const std::string headers = _buf.substr(_pos, end - _pos);
            _pos = end + 4;
            const std::string disposition = headerValue(headers, "Content-Disposition");
            _filename = dispositionParam(disposition, "filename");
            _fieldName = dispositionParam(disposition, "name");
            _partEnded = false;
            _safeEnd = _pos;
            return true;
        }
        if (fill() <= 0) return false;
    }
}

bool MultipartReader::findDelimiter(size_t& index) {
    for (;;) {
        index = _buf.find(_delimiter, _pos);
        if (index != std::string::npos) return true;
        // Keep a tail of delimiter size - 1 so a split delimiter is not missed.
        const size_t keep = _delimiter.size() - 1;
        _safeEnd = _buf.size() > keep ? _buf.size() - keep : _pos;
        if (fill() <= 0) {
            _safeEnd = _buf.size();  // request ended; emit whatever remains
            return false;
        }
    }
}

int MultipartReader::read(uint8_t* dst, int len) {
    if (!_valid || _partEnded || _formEnded || len <= 0) return 0;
    size_t delimAt = 0;
    const bool found = findDelimiter(delimAt);
    const size_t limit = found ? delimAt : _safeEnd;
    if (limit <= _pos) {
        if (found) {
            _pos = delimAt + _delimiter.size();
            _partEnded = true;
            return 0;
        }
        return _eof ? 0 : -1;
    }
    size_t n = limit - _pos;
    if (n > (size_t)len) n = (size_t)len;
    memcpy(dst, _buf.data() + _pos, n);
    _pos += n;
    if (found && _pos == delimAt) {
        _pos = delimAt + _delimiter.size();
        _partEnded = true;
    }
    return (int)n;
}

bool MultipartReader::nextPart() {
    if (!_valid || _formEnded) return false;
    if (!_partEnded) {
        // Drain the current part first.
        uint8_t scratch[1024];
        while (read(scratch, sizeof(scratch)) > 0) {}
        if (!_partEnded) return false;
    }
    // After the delimiter: "--" closes the form, CRLF starts another part.
    for (;;) {
        if (_buf.size() >= _pos + 2) {
            if (_buf.compare(_pos, 2, "--") == 0) {
                _formEnded = true;
                return false;
            }
            if (_buf.compare(_pos, 2, "\r\n") == 0) {
                _pos += 2;
                return parseHeaders();
            }
            return false;
        }
        if (fill() <= 0) return false;
    }
}

}  // namespace lz
