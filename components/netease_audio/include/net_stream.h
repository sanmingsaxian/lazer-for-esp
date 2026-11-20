#pragma once
#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "http_audio_source.h"
#include "mp3dec.h"

namespace lz {

// 24 KiB read-ahead ring buffer with a dedicated feeder task, replacing
// AudioFileSourceBuffer. The decoder never touches the network directly.
class NetStream : public IMp3Source {
public:
    ~NetStream() override;
    bool open(const std::string& url, uint32_t startByte);
    void close();
    bool seek(uint32_t bytePosition);
    uint32_t size() const { return _http.size(); }

    uint32_t read(uint8_t* dst, uint32_t len) override;
    uint32_t pos() const override { return _consumed; }

private:
    static void feederEntry(void* argument);
    void feeder();
    void stopFeeder();

    HttpAudioSource _http;
    uint8_t* _ring = nullptr;
    size_t _capacity = 0;
    size_t _head = 0;  // next write index
    size_t _count = 0;
    uint32_t _consumed = 0;
    volatile bool _stop = false;
    volatile bool _eof = false;
    SemaphoreHandle_t _mutex = nullptr;
    SemaphoreHandle_t _dataReady = nullptr;
    SemaphoreHandle_t _spaceReady = nullptr;
    TaskHandle_t _feeder = nullptr;
};

}  // namespace lz
