#include "AudioEngine.h"
#include <M5Cardputer.h>
#include "Lyrics.h"

AudioEngine Audio;

namespace {
constexpr int I2S_BCLK = 41;
constexpr int I2S_LRCK = 43;
constexpr int I2S_DOUT = 42;
}

bool AudioEngine::begin() {
    M5Cardputer.Speaker.end();
    _output = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 16, AudioOutputI2S::APLL_DISABLE);
    if (!_output || !_output->SetPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT)) {
        snprintf(_error, sizeof(_error), "I2S init failed");
        _state = AudioState::Error;
        return false;
    }
    setVolume(_volume);
    // Keep MP3 decoding off Core 0, where the Wi-Fi/BT protocol tasks run.
    // A larger stack margin is useful when ID3 and decoder frames overlap.
    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "LazerAudio", 20480, this, 4,
                                            &_taskHandle, 1);
    if (ok != pdPASS) {
        snprintf(_error, sizeof(_error), "Audio task failed");
        _state = AudioState::Error;
        return false;
    }
    return true;
}

void AudioEngine::taskEntry(void* argument) {
    static_cast<AudioEngine*>(argument)->task();
}

uint32_t AudioEngine::stackHighWater() const {
    return _taskHandle ? (uint32_t)uxTaskGetStackHighWaterMark(_taskHandle) : 0;
}

void AudioEngine::setVolume(uint8_t volume) {
    _volume = volume > 20 ? 20 : volume;
    if (_output) _output->SetGain((float)_volume / 20.0f * 1.6f);
}

void AudioEngine::play(int index, uint32_t startSeconds) {
    if (!Library.count()) return;
    if (index < 0) index = (int)Library.count() - 1;
    if (index >= (int)Library.count()) index = 0;
    _requestedTrack = index;
    _requestedPosition = startSeconds;
    _currentTrack = index;
    _positionBase = startSeconds;
    _clockStart = millis();
    _wantPlaying = true;
    _state = AudioState::Loading;
    _error[0] = 0;
    _command = Load;
    const Track* track = Library.track(index);
    if (track) Lyrics.clear(track->seed);
}

void AudioEngine::eject() {
    _wantPlaying = false;
    _command = Eject;
    _currentTrack = -1;
    _positionBase = 0;
    _state = AudioState::Empty;
    Lyrics.clear(0);
}

void AudioEngine::toggle() {
    if (_state == AudioState::Empty || _currentTrack < 0) {
        if (Library.count()) play(0);
        return;
    }
    if (_wantPlaying) pause();
    else resume();
}

void AudioEngine::pause() {
    if (!_wantPlaying) return;
    _positionBase = position();
    _wantPlaying = false;
    if (_state != AudioState::Error) _state = AudioState::Paused;
}

void AudioEngine::resume() {
    if (_currentTrack < 0 || _state == AudioState::Loading) return;
    if (_state == AudioState::Error || !_mp3) {
        play(_currentTrack, _positionBase);
        return;
    }
    _clockStart = millis();
    _wantPlaying = true;
    _state = AudioState::Playing;
}

void AudioEngine::next(bool automatic) {
    if (!Library.count()) return;
    play(nextIndex(automatic));
}

void AudioEngine::previous() {
    if (position() > 5 && _currentTrack >= 0) {
        play(_currentTrack);
        return;
    }
    if (!Library.count()) return;
    int index = _currentTrack < 0 ? 0 : _currentTrack - 1;
    if (index < 0) index = (int)Library.count() - 1;
    play(index);
}

int AudioEngine::nextIndex(bool automatic) const {
    const int count = (int)Library.count();
    if (!count) return -1;
    if (_mode == PlayMode::RepeatOne && automatic) return _currentTrack;
    if (_mode == PlayMode::Shuffle && count > 1) {
        int candidate = _currentTrack;
        while (candidate == _currentTrack) candidate = random(count);
        return candidate;
    }
    int index = _currentTrack + 1;
    if (index >= count) index = 0;
    return index;
}

void AudioEngine::seek(int deltaSeconds) {
    if (_currentTrack < 0 || _state == AudioState::Loading) return;
    int64_t target = (int64_t)position() + deltaSeconds;
    if (target < 0) target = 0;
    if (duration() && target >= duration()) target = duration() > 1 ? duration() - 1 : 0;
    _requestedTrack = _currentTrack;
    _requestedPosition = (uint32_t)target;
    _positionBase = (uint32_t)target;
    _clockStart = millis();
    _command = Seek;
}

bool AudioEngine::storageBegin() {
    if (_storageRequested) return _storageReady;
    _storageTrack = _currentTrack;
    const Track* current = _storageTrack >= 0 ? Library.track(_storageTrack) : nullptr;
    _storagePath = current ? current->path : "";
    _storagePosition = position();
    _storageResume = _wantPlaying && _currentTrack >= 0;
    _storageReady = false;
    _storageRequested = true;
    _wantPlaying = false;
    if (_state != AudioState::Error) {
        _state = _currentTrack >= 0 ? AudioState::Paused : AudioState::Empty;
    }
    _command = Eject;
    if (!_taskHandle) {
        _storageReady = true;
        return true;
    }
    const uint32_t started = millis();
    while (!_storageReady && millis() - started < 2500) delay(2);
    return _storageReady;
}

void AudioEngine::storageEnd() {
    if (!_storageRequested) return;
    const bool resume = _storageResume;
    const String path = _storagePath;
    const uint32_t savedPosition = _storagePosition;
    _storageResume = false;
    _storagePath = "";
    _storageRequested = false;
    _storageReady = false;
    const int track = path.length() ? Library.findByPath(path) : -1;
    if (track >= 0 && resume) {
        play(track, savedPosition);
    } else if (track >= 0) {
        // Keep a paused selection usable. resume() will reload the decoder,
        // which was deliberately destroyed while the web session owned SD.
        _currentTrack = track;
        _positionBase = savedPosition;
        _clockStart = millis();
        _wantPlaying = false;
        _state = AudioState::Paused;
        _command = None;
    } else {
        eject();
    }
}

uint32_t AudioEngine::position() const {
    if (!_wantPlaying || _state == AudioState::Loading) return _positionBase;
    uint32_t value = _positionBase + (millis() - _clockStart) / 1000;
    uint32_t length = duration();
    if (length && value > length) value = length;
    return value;
}

uint32_t AudioEngine::duration() const {
    const Track* track = _currentTrack >= 0 ? Library.track(_currentTrack) : nullptr;
    return track ? track->duration : 0;
}

void AudioEngine::destroyDecoder() {
    if (_mp3) {
        if (_mp3->isRunning()) _mp3->stop();
        delete _mp3;
        _mp3 = nullptr;
    }
    if (_id3) {
        delete _id3;
        _id3 = nullptr;
    }
    if (_source) {
        delete _source;
        _source = nullptr;
    }
}

bool AudioEngine::loadTrack(int index, uint32_t startSeconds) {
    destroyDecoder();
    const Track* track = Library.track(index);
    if (!track) {
        _state = AudioState::Empty;
        return false;
    }
    Lyrics.load(track->path, track->seed);
    _source = new AudioFileSourceSD(track->path.c_str());
    _id3 = _source ? new AudioFileSourceID3(_source) : nullptr;
    _mp3 = _id3 ? new AudioGeneratorMP3() : nullptr;
    if (!_source || !_id3 || !_mp3) {
        snprintf(_error, sizeof(_error), "Not enough memory for MP3");
        Serial.printf("MP3_MEMORY_ERROR freeHeap=%u maxAlloc=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        _wantPlaying = false;
        _state = AudioState::Error;
        destroyDecoder();
        return false;
    }
    if (startSeconds && track->bitrate) {
        uint64_t bytePosition = ((uint64_t)startSeconds * track->bitrate) / 8;
        if (bytePosition < track->fileSize) _id3->seek((uint32_t)bytePosition, SEEK_SET);
    }
    if (!_mp3->begin(_id3, _output)) {
        snprintf(_error, sizeof(_error), "Cannot decode MP3");
        _wantPlaying = false;
        _state = AudioState::Error;
        destroyDecoder();
        return false;
    }
    _positionBase = startSeconds;
    _clockStart = millis();
    _wantPlaying = true;
    _state = AudioState::Playing;
    Serial.printf("MP3_STARTED track=%d freeHeap=%u minFreeHeap=%u audioStackHwm=%u\n",
                  index, ESP.getFreeHeap(), ESP.getMinFreeHeap(), stackHighWater());
    return true;
}

void AudioEngine::task() {
    bool decoderWasRunning = false;
    uint32_t lastReport = millis();
    for (;;) {
        if (_storageRequested) {
            if (!_storageReady) {
                destroyDecoder();
                decoderWasRunning = false;
                _storageReady = true;
            }
            vTaskDelay(pdMS_TO_TICKS(3));
            continue;
        }
        Command command = _command;
        if (command != None) {
            _command = None;
            if (command == Load || command == Seek) {
                loadTrack(_requestedTrack, _requestedPosition);
                decoderWasRunning = false;
            } else if (command == Eject) {
                destroyDecoder();
                decoderWasRunning = false;
            }
        }

        if (_wantPlaying && _mp3 && _mp3->isRunning()) {
            decoderWasRunning = true;
            if (!_mp3->loop()) {
                if (_mp3->isRunning()) _mp3->stop();
                if (_wantPlaying) next(true);
            }
        } else {
            if (decoderWasRunning && !_wantPlaying) {
                _positionBase = position();
                decoderWasRunning = false;
            }
            vTaskDelay(pdMS_TO_TICKS(4));
        }
        if (_wantPlaying && millis() - lastReport >= 10000) {
            lastReport = millis();
            Serial.printf("AUDIO_ALIVE pos=%lu freeHeap=%u minFreeHeap=%u audioStackHwm=%u\n",
                          (unsigned long)position(), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                          stackHighWater());
        }
        // A real delay also yields to lower-priority Arduino/Web tasks on Core 1;
        // taskYIELD() alone only considers tasks at the same or higher priority.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
