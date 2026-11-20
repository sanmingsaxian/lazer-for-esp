#pragma once
#include <cstdint>
#include <string>

#include <esp_http_server.h>

#define LAZER_VERSION "2.0.0-idf"

namespace lz {

// Port of WebControl: esp_http_server with the exact v1.3.0 API contract.
class WebControl {
public:
    void begin();
    void update();

    bool enabled() const { return _running; }
    std::string address() const;
    std::string connectionLabel() const;

    httpd_handle_t server() { return _server; }
    const std::string& password() const { return _activeWebPassword; }
    void scheduleReboot(uint32_t delayMs);
    void scheduleWifiConnect(uint32_t delayMs);
    void cacheSDCapacity();
    void refreshLibrary();
    uint64_t sdTotal() const { return _sdTotal; }
    uint64_t sdUsed() const { return _sdUsed; }

private:
    httpd_handle_t _server = nullptr;
    bool _running = false;
    std::string _activeWebPassword;
    uint64_t _sdTotal = 0;
    uint64_t _sdUsed = 0;
    uint32_t _rebootAt = 0;
    uint32_t _wifiConnectAt = 0;
};

extern WebControl Web;

}  // namespace lz
