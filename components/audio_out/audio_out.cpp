#include "audio_out.h"
#include "pins.h"

#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <cstring>

namespace lz {

namespace {
i2s_chan_handle_t s_tx = nullptr;
}

bool AudioOutput::begin() {
    if (_running) return true;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 16;   // v1.1.1: raised from 12
    chan.dma_frame_num = 240;
    chan.auto_clear = true;
    if (i2s_new_channel(&chan, &s_tx, nullptr) != ESP_OK) return false;

    i2s_std_config_t std = {};
    const i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(_rate);
    const i2s_std_slot_config_t slot = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    std.clk_cfg = clk;
    std.slot_cfg = slot;
    std.gpio_cfg = {};
    std.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std.gpio_cfg.bclk = (gpio_num_t)pins::I2S_BCLK;
    std.gpio_cfg.ws = (gpio_num_t)pins::I2S_LRCK;
    std.gpio_cfg.dout = (gpio_num_t)pins::I2S_DOUT;
    std.gpio_cfg.din = I2S_GPIO_UNUSED;
    std.gpio_cfg.invert_flags.mclk_inv = false;
    std.gpio_cfg.invert_flags.bclk_inv = false;
    std.gpio_cfg.invert_flags.ws_inv = false;

    if (i2s_channel_init_std_mode(s_tx, &std) != ESP_OK) {
        i2s_del_channel(s_tx);
        s_tx = nullptr;
        return false;
    }
    if (i2s_channel_enable(s_tx) != ESP_OK) {
        i2s_del_channel(s_tx);
        s_tx = nullptr;
        return false;
    }
    _running = true;
    return true;
}

void AudioOutput::end() {
    if (!s_tx) return;
    i2s_channel_disable(s_tx);
    i2s_del_channel(s_tx);
    s_tx = nullptr;
    _running = false;
}

bool AudioOutput::setRate(uint32_t rate) {
    if (rate == _rate) return true;
    if (!s_tx) {
        _rate = rate;
        return true;
    }
    i2s_channel_disable(s_tx);
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    esp_err_t err = i2s_channel_reconfig_std_clock(s_tx, &clk);
    i2s_channel_enable(s_tx);
    if (err == ESP_OK) _rate = rate;
    return err == ESP_OK;
}

void AudioOutput::setGain(float gain) {
    if (gain < 0) gain = 0;
    if (gain > 1.6f) gain = 1.6f;
    _gain = gain;
}

size_t AudioOutput::write(const int16_t* samples, size_t count, uint32_t timeoutMs) {
    if (!_running || !s_tx || !count) return 0;
    // Software gain in blocks; unity gain writes straight through.
    if (_gain > 0.999f && _gain < 1.001f) {
        size_t written = 0;
        i2s_channel_write(s_tx, samples, count * sizeof(int16_t), &written,
                          pdMS_TO_TICKS(timeoutMs));
        return written / sizeof(int16_t);
    }
    int16_t block[512];
    size_t total = 0;
    while (total < count) {
        const size_t n = count - total < 512 ? count - total : 512;
        for (size_t i = 0; i < n; ++i) {
            int32_t v = (int32_t)(samples[total + i] * _gain);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            block[i] = (int16_t)v;
        }
        size_t written = 0;
        if (i2s_channel_write(s_tx, block, n * sizeof(int16_t), &written,
                              pdMS_TO_TICKS(timeoutMs)) != ESP_OK)
            break;
        total += written / sizeof(int16_t);
        if (written < n * sizeof(int16_t)) break;
    }
    return total;
}

}  // namespace lz
