#include "net_stream.h"
#include "board.h"

#include <cstring>
#include <esp_log.h>

namespace lz {

namespace {
constexpr size_t kNetworkBuffer = 24 * 1024;
constexpr char kTag[] = "NetStream";
}  // namespace

NetStream::~NetStream() { close(); }

bool NetStream::open(const std::string& url, uint32_t startByte) {
    close();
    if (!_http.open(url, startByte)) return false;
    _ring = (uint8_t*)malloc(kNetworkBuffer);
    if (!_ring) {
        _http.close();
        return false;
    }
    _capacity = kNetworkBuffer;
    _head = 0;
    _count = 0;
    _consumed = startByte;
    _stop = false;
    _eof = false;
    _mutex = xSemaphoreCreateMutex();
    _dataReady = xSemaphoreCreateBinary();
    _spaceReady = xSemaphoreCreateBinary();
    xSemaphoreGive(_spaceReady);
    if (!_mutex || !_dataReady || !_spaceReady) {
        close();
        return false;
    }
    // Feeder on Core 0 below the decoder; network I/O must not starve decode.
    BaseType_t ok = xTaskCreatePinnedToCore(feederEntry, "LazerNet", 4096, this, 3,
                                            &_feeder, 0);
    if (ok != pdPASS) {
        _feeder = nullptr;
        close();
        return false;
    }
    return true;
}

void NetStream::stopFeeder() {
    if (!_feeder) return;
    _stop = true;
    xSemaphoreGive(_dataReady);
    xSemaphoreGive(_spaceReady);
    const uint32_t started = millis();
    while (_feeder && millis() - started < 3000) vTaskDelay(pdMS_TO_TICKS(5));
    _feeder = nullptr;
}

void NetStream::close() {
    stopFeeder();
    _http.close();
    free(_ring);
    _ring = nullptr;
    _count = 0;
    if (_mutex) { vSemaphoreDelete(_mutex); _mutex = nullptr; }
    if (_dataReady) { vSemaphoreDelete(_dataReady); _dataReady = nullptr; }
    if (_spaceReady) { vSemaphoreDelete(_spaceReady); _spaceReady = nullptr; }
}

bool NetStream::seek(uint32_t bytePosition) {
    stopFeeder();
    if (!_http.seek(bytePosition)) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _head = 0;
    _count = 0;
    _consumed = bytePosition;
    _eof = false;
    xSemaphoreGive(_mutex);
    _stop = false;
    xSemaphoreGive(_spaceReady);
    BaseType_t ok = xTaskCreatePinnedToCore(feederEntry, "LazerNet", 4096, this, 3,
                                            &_feeder, 0);
    return ok == pdPASS;
}

void NetStream::feederEntry(void* argument) {
    static_cast<NetStream*>(argument)->feeder();
}

void NetStream::feeder() {
    uint8_t chunk[1024];
    while (!_stop) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        size_t freeSpace = _capacity - _count;
        xSemaphoreGive(_mutex);
        if (freeSpace == 0) {
            xSemaphoreTake(_spaceReady, pdMS_TO_TICKS(100));
            continue;
        }
        const uint32_t wanted = freeSpace < sizeof(chunk) ? (uint32_t)freeSpace
                                                          : (uint32_t)sizeof(chunk);
        const uint32_t got = _http.read(chunk, wanted);
        if (got == 0) {
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _eof = true;
            xSemaphoreGive(_mutex);
            xSemaphoreGive(_dataReady);
            break;
        }
        xSemaphoreTake(_mutex, portMAX_DELAY);
        size_t first = _capacity - _head;
        if (first > got) first = got;
        memcpy(_ring + _head, chunk, first);
        if (got > first) memcpy(_ring, chunk + first, got - first);
        _head = (_head + got) % _capacity;
        _count += got;
        xSemaphoreGive(_mutex);
        xSemaphoreGive(_dataReady);
    }
    _feeder = nullptr;
    vTaskDelete(nullptr);
}

uint32_t NetStream::read(uint8_t* dst, uint32_t len) {
    if (!_ring || !dst || !len) return 0;
    uint32_t waited = 0;
    for (;;) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        if (_count) {
            size_t tail = (_head + _capacity - _count) % _capacity;
            size_t n = len < _count ? len : _count;
            size_t first = _capacity - tail;
            if (first > n) first = n;
            memcpy(dst, _ring + tail, first);
            if (n > first) memcpy(dst + first, _ring, n - first);
            _count -= n;
            _consumed += (uint32_t)n;
            xSemaphoreGive(_mutex);
            xSemaphoreGive(_spaceReady);
            return (uint32_t)n;
        }
        const bool eof = _eof;
        xSemaphoreGive(_mutex);
        if (eof || _stop) return 0;
        if (waited >= 2500) return 0;  // feeder stalled; let decoder retry/stop
        xSemaphoreTake(_dataReady, pdMS_TO_TICKS(100));
        waited += 100;
    }
}

}  // namespace lz
