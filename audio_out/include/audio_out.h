#pragma once
#include <cstddef>
#include <cstdint>

namespace lz {

// NS4168 I2S amplifier on I2S0: BCLK41 / LRCK43 / DOUT42.
// 16 DMA descriptors (v1.1.1 stability tuning), stereo 16-bit.
class AudioOutput {
public:
    bool begin();
    void end();
    bool setRate(uint32_t rate);
    // Blocking write with timeout; returns bytes accepted.
    size_t write(const int16_t* samples, size_t count, uint32_t timeoutMs);
    void setGain(float gain);  // 0..1.6, applied in software
    float gain() const { return _gain; }
    uint32_t rate() const { return _rate; }
    bool running() const { return _running; }

private:
    bool _running = false;
    uint32_t _rate = 44100;
    float _gain = 0.96f;
};

}  // namespace lz
