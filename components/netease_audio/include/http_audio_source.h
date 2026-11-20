#pragma once
#include <cstdint>
#include <string>

#include <esp_http_client.h>

namespace lz {

// Port of NeteaseAudioSource: HTTP Range streaming with manual redirects (<=4),
// limited reconnects (<=2) and strict 206 enforcement when seeking.
// TLS intentionally skips certificate verification, matching the original
// setInsecure() policy documented in the README.
class HttpAudioSource {
public:
    ~HttpAudioSource();
    bool open(const std::string& url, uint32_t startPosition = 0);
    void close();
    bool isOpen() const { return _open; }
    uint32_t size() const { return _size; }
    uint32_t pos() const { return _position; }
    // Blocking read; 0 means EOF or failed reconnect.
    uint32_t read(uint8_t* dst, uint32_t len);
    bool seek(uint32_t bytePosition);

private:
    bool connect(uint32_t startPosition);
    bool reconnect();
    static esp_err_t eventHandler(esp_http_client_event_t* event);

    esp_http_client_handle_t _client = nullptr;
    std::string _url;
    std::string _location;
    uint32_t _position = 0;
    uint32_t _size = 0;
    uint8_t _redirects = 0;
    uint8_t _reconnects = 0;
    bool _open = false;
};

}  // namespace lz
