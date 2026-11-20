#include "mp3dec.h"

#include <cstdlib>
#include <cstring>

extern "C" {
#include "mad.h"
}

// Declared by libmad's config.h; the stackenter() macro compiles to nothing,
// but provide a definition so any future reference links.
extern "C" int stackfree() { return 65536; }

namespace lz {

namespace {
// Same buffer size as AudioGeneratorMP3: slightly larger than the largest
// MP3 frame (1441 bytes for 320 kbps / 32 kHz).
constexpr int kBuffLen = 0x600;
}  // namespace

Mp3Decoder::~Mp3Decoder() { stop(); }

bool Mp3Decoder::begin(IMp3Source* source) {
    stop();
    if (!source) return false;
    _source = source;
    _buff = (uint8_t*)malloc(kBuffLen);
    _stream = (mad_stream*)malloc(sizeof(mad_stream));
    _frame = (mad_frame*)malloc(sizeof(mad_frame));
    _synth = (mad_synth*)malloc(sizeof(mad_synth));
    if (!_buff || !_stream || !_frame || !_synth) {
        stop();
        return false;
    }
    mad_stream_init(_stream);
    mad_frame_init(_frame);
    mad_synth_init(_synth);
    _initted = true;
    _lastBuffLen = 0;
    _lastReadPos = 0;
    _unrecoverable = 0;
    _samplePtr = 32;
    _nsCount = 0;
    _nsCountMax = 36;
    _synth->pcm.length = 0;
    _rate = 44100;
    _channels = 2;
    _running = true;
    return true;
}

void Mp3Decoder::stop() {
    if (_initted) {
        mad_synth_finish(_synth);
        mad_frame_finish(_frame);
        mad_stream_finish(_stream);
        _initted = false;
    }
    free(_buff);
    free(_stream);
    free(_frame);
    free(_synth);
    _buff = nullptr;
    _stream = nullptr;
    _frame = nullptr;
    _synth = nullptr;
    _source = nullptr;
    _running = false;
}

void Mp3Decoder::desync() {
    if (_stream) {
        _stream->next_frame = nullptr;
        _stream->this_frame = nullptr;
        _stream->sync = 0;
    }
    _lastBuffLen = 0;
}

bool Mp3Decoder::input() {
    int unused = 0;
    if (_stream->next_frame) {
        unused = _lastBuffLen - (int)(_stream->next_frame - _buff);
        if (unused < 0) {
            desync();
            unused = 0;
        } else {
            memmove(_buff, _stream->next_frame, unused);
        }
        _stream->next_frame = nullptr;
    }
    if (unused == _lastBuffLen) unused = 0;

    bool foundHeader = false;
    do {
        _lastReadPos = _source->pos() - unused;
        int len = (int)_source->read(_buff + unused, kBuffLen - unused);
        if (len == 0 && unused == 0) return false;  // EOF
        _lastBuffLen = len + unused;
        for (int i = 0; i + 1 < _lastBuffLen; ++i) {
            if (_buff[i] == 0xff && (_buff[i + 1] & 0xe0) == 0xe0) {
                if (i) {
                    memmove(_buff, _buff + i, _lastBuffLen - i);
                    _lastBuffLen -= i;
                }
                foundHeader = true;
                break;
            }
        }
        if (!foundHeader) {
            unused = 0;
        } else {
            len = (int)_source->read(_buff + _lastBuffLen, kBuffLen - _lastBuffLen);
            _lastBuffLen += len;
            if (_lastBuffLen < 8) return false;
        }
    } while (!foundHeader);

    mad_stream_buffer(_stream, _buff, _lastBuffLen);
    return true;
}

size_t Mp3Decoder::decode(int16_t* out, size_t maxPairs) {
    if (!_running || !out || !maxPairs) return 0;
    size_t produced = 0;
    while (produced < maxPairs) {
        if (_samplePtr >= _synth->pcm.length && _nsCount >= _nsCountMax) {
            for (;;) {
                if (!input()) {
                    _running = false;
                    return produced;
                }
                if (mad_frame_decode(_frame, _stream) != -1) break;
                // Tolerate recoverable errors exactly like the original:
                // only repeated BUFLEN after seeking is fatal.
                if (_stream->error == MAD_ERROR_BUFLEN) {
                    if (++_unrecoverable >= 3) {
                        _running = false;
                        return produced;
                    }
                } else {
                    _unrecoverable = 0;
                }
            }
            _nsCountMax = MAD_NSBSAMPLES(&_frame->header);
            _nsCount = 0;
            _samplePtr = 32;  // force synthesis of the first block
        }
        if (_samplePtr >= _synth->pcm.length) {
            mad_synth_frame_onens(_synth, _frame, _nsCount++);
            _samplePtr = 0;
            _rate = _synth->pcm.samplerate;
            _channels = _synth->pcm.channels;
        }
        const size_t avail = _synth->pcm.length - _samplePtr;
        size_t n = maxPairs - produced;
        if (n > avail) n = avail;
        for (size_t i = 0; i < n; ++i) {
            const int16_t left = _synth->pcm.samples[0][_samplePtr + i];
            const int16_t right =
                _channels == 2 ? _synth->pcm.samples[1][_samplePtr + i] : left;
            out[2 * (produced + i)] = left;
            out[2 * (produced + i) + 1] = right;
        }
        _samplePtr += (int)n;
        produced += n;
    }
    return produced;
}

}  // namespace lz
