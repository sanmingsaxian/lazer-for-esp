#pragma once
#include <cstdint>
#include <string>

#include <esp_http_server.h>

namespace lz {

// Streaming multipart/form-data parser for esp_http_server (which has none).
// Handles one part at a time; file payloads never sit fully in RAM.
class MultipartReader {
public:
    bool begin(httpd_req_t* req);
    bool valid() const { return _valid; }
    std::string filename() const { return _filename; }
    std::string fieldName() const { return _fieldName; }

    // Returns bytes copied (>0), 0 at end of the current part, -1 on error.
    int read(uint8_t* dst, int len);
    // Advance to the next part; false when the form is finished.
    bool nextPart();

private:
    int fill();
    bool parseHeaders();
    bool findDelimiter(size_t& index);

    httpd_req_t* _req = nullptr;
    std::string _delimiter;  // "\r\n--<boundary>"
    std::string _buf;
    size_t _pos = 0;
    size_t _safeEnd = 0;  // bytes before this index are safe to emit
    bool _valid = false;
    bool _partEnded = false;
    bool _formEnded = false;
    bool _eof = false;
    std::string _filename;
    std::string _fieldName;
};

}  // namespace lz
