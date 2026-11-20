#include "http_audio_source.h"
#include "strutil.h"

#include <cstring>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace lz {

namespace {
constexpr char kTag[] = "HttpAudio";

std::string resolveLocation(const std::string& base, const std::string& location) {
    if (startsWith(location, "http://") || startsWith(location, "https://")) return location;
    if (!startsWith(location, "/")) return "";
    const size_t scheme = base.find("://");
    const size_t hostStart = scheme == std::string::npos ? 0 : scheme + 3;
    size_t slash = base.find('/', hostStart);
    if (slash == std::string::npos) slash = base.size();
    return base.substr(0, slash) + location;
}
}  // namespace

HttpAudioSource::~HttpAudioSource() { close(); }

esp_err_t HttpAudioSource::eventHandler(esp_http_client_event_t* event) {
    auto* self = static_cast<HttpAudioSource*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value) {
        if (strcasecmp(event->header_key, "Location") == 0)
            self->_location = event->header_value;
    }
    return ESP_OK;
}

bool HttpAudioSource::open(const std::string& url, uint32_t startPosition) {
    close();
    if (!startsWith(url, "http://") && !startsWith(url, "https://")) return false;
    _url = url;
    _position = 0;
    _size = 0;
    _redirects = 0;
    _reconnects = 0;
    return connect(startPosition);
}

bool HttpAudioSource::connect(uint32_t startPosition) {
    if (_client) {
        esp_http_client_close(_client);
        esp_http_client_cleanup(_client);
        _client = nullptr;
    }
    _open = false;
    _location.clear();

    esp_http_client_config_t config = {};
    config.url = _url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 8000;
    config.buffer_size = 4096;
    config.disable_auto_redirect = true;
    config.event_handler = eventHandler;
    config.user_data = this;
    config.user_agent = "Lazer-Cardputer/2.0";
    // No cert_pem / CA store: transport encryption without certificate
    // authentication, identical to the original setInsecure() behaviour.
    _client = esp_http_client_init(&config);
    if (!_client) return false;
    if (startPosition) {
        char range[48];
        snprintf(range, sizeof(range), "bytes=%lu-", (unsigned long)startPosition);
        esp_http_client_set_header(_client, "Range", range);
    }
    if (esp_http_client_open(_client, 0) != ESP_OK) {
        esp_http_client_cleanup(_client);
        _client = nullptr;
        return false;
    }
    esp_http_client_fetch_headers(_client);
    const int status = esp_http_client_get_status_code(_client);

    if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) &&
        _redirects < 4) {
        const std::string location = resolveLocation(_url, _location);
        esp_http_client_close(_client);
        esp_http_client_cleanup(_client);
        _client = nullptr;
        if (location.empty()) return false;
        _url = location;
        ++_redirects;
        return connect(startPosition);
    }
    if (status != 200 && status != 206) {
        ESP_LOGW(kTag, "unexpected status %d", status);
        esp_http_client_close(_client);
        esp_http_client_cleanup(_client);
        _client = nullptr;
        return false;
    }
    if (startPosition && status != 206) {
        // Ignoring Range would decode the beginning while reporting a later time.
        esp_http_client_close(_client);
        esp_http_client_cleanup(_client);
        _client = nullptr;
        return false;
    }
    const int64_t remaining = esp_http_client_get_content_length(_client);
    _size = remaining > 0 ? startPosition + (uint32_t)remaining : 0;
    _position = startPosition;
    _open = true;
    return true;
}

bool HttpAudioSource::reconnect() {
    if (_reconnects >= 2 || _url.empty()) return false;
    ++_reconnects;
    const uint32_t position = _position;
    _redirects = 0;
    vTaskDelay(pdMS_TO_TICKS(120));
    return connect(position);
}

uint32_t HttpAudioSource::read(uint8_t* dst, uint32_t len) {
    if (!dst || !len || !_open) return 0;
    if (_size && _position >= _size) return 0;
    int n = esp_http_client_read(_client, (char*)dst, len);
    if (n < 0) {
        if (reconnect()) return read(dst, len);
        _open = false;
        return 0;
    }
    if (n == 0) {
        if (esp_http_client_is_complete_data_received(_client)) return 0;  // EOF
        if (reconnect()) return read(dst, len);
        _open = false;
        return 0;
    }
    _position += (uint32_t)n;
    _reconnects = 0;
    return (uint32_t)n;
}

bool HttpAudioSource::seek(uint32_t bytePosition) {
    _redirects = 0;
    _reconnects = 0;
    return connect(bytePosition);
}

void HttpAudioSource::close() {
    _open = false;
    if (_client) {
        esp_http_client_close(_client);
        esp_http_client_cleanup(_client);
        _client = nullptr;
    }
}

}  // namespace lz
