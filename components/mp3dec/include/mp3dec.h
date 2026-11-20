#pragma once
#include <cstddef>
#include <cstdint>

// libmad types live in the global namespace (C library).
extern "C" {
struct mad_stream;
struct mad_frame;
struct mad_synth;
}

namespace lz {

// Blocking byte source feeding the decoder (SD file or buffered network stream).
struct IMp3Source {
    virtual ~IMp3Source() = default;
    virtual uint32_t read(uint8_t* dst, uint32_t len) = 0;  // 0 = EOF/error
    virtual uint32_t pos() const = 0;
};

// libmad wrapper ported from ESP8266Audio's AudioGeneratorMP3: sync search,
// BUFLEN retry budget and incremental 32-sample synthesis blocks.
class Mp3Decoder {
public:
    ~Mp3Decoder();
    bool begin(IMp3Source* source);
    void stop();
    bool running() const { return _running; }

    // Fill up to maxPairs stereo sample pairs; returns pairs produced.
    // 0 with running() == false means end of stream or fatal decode failure.
    size_t decode(int16_t* out, size_t maxPairs);

    uint32_t sampleRate() const { return _rate; }
    int channels() const { return _channels; }

private:
    bool input();
    void desync();

    IMp3Source* _source = nullptr;
    uint8_t* _buff = nullptr;
    struct mad_stream* _stream = nullptr;
    struct mad_frame* _frame = nullptr;
    struct mad_synth* _synth = nullptr;
    int _lastBuffLen = 0;    uint32_t _lastReadPos = 0;
    int _samplePtr = 0;
    int _nsCount = 0;
    int _nsCountMax = 36;
    int _unrecoverable = 0;
    bool _initted = false;
    bool _running = false;
    uint32_t _rate = 44100;
    int _channels = 2;
};

}  // namespace lz
