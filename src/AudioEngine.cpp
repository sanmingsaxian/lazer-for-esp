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
    _output = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 12, AudioOutputI2S::APLL_DISABLE);
    if (!_output || !_output->SetPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT)) {
        snprintf(_error, sizeof(_error), "I2S init failed");
        _state = AudioState::Error;
        return false;
    }
    setVolume(_volume);
    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "LazerAudio", 16384, this, 3,
                                            &_taskHandle, 0);
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
    if (_state == AudioState::Error) {
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
    _id3 = new AudioFileSourceID3(_source);
    _mp3 = new AudioGeneratorMP3();
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
    return true;
}

void AudioEngine::task() {
    bool decoderWasRunning = false;
    for (;;) {
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
        taskYIELD();
    }
}
