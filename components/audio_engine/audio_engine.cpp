#include "audio_engine.h"
#include "board.h"
#include "id3.h"
#include "mp3info.h"
#include "lyrics.h"
#include "netease.h"
#include "storage.h"
#include "strutil.h"

#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_system.h>
#include <new>

namespace lz {

AudioEngine Audio;

namespace {
uint32_t freeHeap() { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT); }
uint32_t maxAlloc() { return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); }

// SD file source with the ID3v2 skip behaviour of AudioFileSourceID3.
class SdFileSource : public IMp3Source {
public:
    ~SdFileSource() override { close(); }
    bool open(const std::string& vfsPath) {
        _file = fopen(vfsPath.c_str(), "rb");
        if (!_file) return false;
        fseek(_file, 0, SEEK_END);
        long end = ftell(_file);
        _size = end > 0 ? (uint32_t)end : 0;
        fseek(_file, 0, SEEK_SET);
        uint8_t header[10];
        if (fread(header, 1, 10, _file) == 10 && memcmp(header, "ID3", 3) == 0) {
            const uint32_t offset = 10 + synchsafe(header + 6);
            fseek(_file, offset < _size ? offset : 0, SEEK_SET);
        } else {
            fseek(_file, 0, SEEK_SET);
        }
        return true;
    }
    void close() {
        if (_file) { fclose(_file); _file = nullptr; }
    }
    uint32_t read(uint8_t* dst, uint32_t len) override {
        if (!_file) return 0;
        return (uint32_t)fread(dst, 1, len, _file);
    }
    uint32_t pos() const override { return _file ? (uint32_t)ftell(_file) : 0; }
    bool seek(uint32_t bytePosition) {
        return _file && fseek(_file, bytePosition, SEEK_SET) == 0;
    }
    uint32_t size() const { return _size; }

private:
    FILE* _file = nullptr;
    uint32_t _size = 0;
};
}  // namespace

bool AudioEngine::begin() {
    _output = new (std::nothrow) AudioOutput();
    if (!_output || !_output->begin()) {
        snprintf(_error, sizeof(_error), "I2S init failed");
        _state = AudioState::Error;
        return false;
    }
    setVolume(_volume);
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

uint32_t AudioEngine::onlineSeed() const {
    return 0x80000000UL | (uint32_t)(_onlineTrack.id ^ (_onlineTrack.id >> 32));
}

void AudioEngine::setVolume(uint8_t volume) {
    _volume = volume > 20 ? 20 : volume;
    if (_output) _output->setGain((float)_volume / 20.0f * 1.6f);
}

void AudioEngine::play(int index, uint32_t startSeconds) {
    if (!Library.count()) return;
    if (index < 0) index = (int)Library.count() - 1;
    if (index >= (int)Library.count()) index = 0;
    _onlineActive = false;
    _onlineTrack = OnlineTrack();
    _onlinePlayback = OnlinePlayback();
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

void AudioEngine::playOnline(const OnlineTrack& track, const OnlinePlayback& playback,
                             uint32_t startSeconds) {
    _onlinePlayback = OnlinePlayback();
    _pendingOnlineTrack = track;
    _pendingOnlinePlayback = playback;
    _requestedPosition = startSeconds;
    _currentTrack = -1;
    _onlineActive = true;
    _onlineTrack = track;
    _positionBase = startSeconds;
    _clockStart = millis();
    _wantPlaying = true;
    _state = AudioState::Loading;
    _error[0] = 0;
    _command = LoadOnline;
    Lyrics.clear(onlineSeed());
}

void AudioEngine::reloadOnline(uint32_t startSeconds) {
    if (!_onlineActive || !_onlineTrack.id) return;
    _pendingOnlineTrack = _onlineTrack;
    _pendingOnlinePlayback = _onlinePlayback;
    _onlinePlayback = OnlinePlayback();
    _requestedPosition = startSeconds;
    _positionBase = startSeconds;
    _clockStart = millis();
    _wantPlaying = true;
    _state = AudioState::Loading;
    _error[0] = 0;
    _command = LoadOnline;
    Lyrics.clear(onlineSeed());
}

void AudioEngine::eject() {
    _wantPlaying = false;
    _command = Eject;
    _currentTrack = -1;
    _onlineActive = false;
    _onlineTrack = OnlineTrack();
    _onlinePlayback = OnlinePlayback();
    _positionBase = 0;
    _state = AudioState::Empty;
    Lyrics.clear(0);
}

void AudioEngine::toggle() {
    if (_state == AudioState::Empty || !hasSelection()) {
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
    if (!hasSelection() || _state == AudioState::Loading) return;
    if (_state == AudioState::Error || !_mp3) {
        if (_onlineActive) reloadOnline(_positionBase);
        else play(_currentTrack, _positionBase);
        return;
    }
    _clockStart = millis();
    _wantPlaying = true;
    _state = AudioState::Playing;
}

void AudioEngine::next(bool automatic) {
    if (_onlineActive) {
        _requestedOnlineDelta = (_mode == PlayMode::RepeatOne && automatic) ? 0 : 1;
        _wantPlaying = true;
        _state = AudioState::Loading;
        _positionBase = 0;
        _command = ResolveOnline;
        return;
    }
    if (!Library.count()) return;
    play(nextIndex(automatic));
}

void AudioEngine::previous() {
    if (position() > 5 && hasSelection()) {
        if (_onlineActive) reloadOnline();
        else play(_currentTrack);
        return;
    }
    if (_onlineActive) {
        _requestedOnlineDelta = -1;
        _wantPlaying = true;
        _state = AudioState::Loading;
        _positionBase = 0;
        _command = ResolveOnline;
        return;
    }
    if (!Library.count()) return;
    play(previousTrackIndex(_currentTrack, (int)Library.count()));
}

int AudioEngine::nextIndex(bool automatic) const {
    return nextTrackIndex(_mode, _currentTrack, (int)Library.count(), automatic, esp_random());
}

void AudioEngine::seek(int deltaSeconds) {
    if (!hasSelection() || _state == AudioState::Loading) return;
    const uint32_t target = clampSeek((int64_t)position() + deltaSeconds, duration());
    if (_onlineActive) {
        reloadOnline(target);
        return;
    }
    _requestedTrack = _currentTrack;
    _requestedPosition = target;
    _positionBase = target;
    _clockStart = millis();
    _command = Seek;
}

bool AudioEngine::storageBegin() {
    if (_storageRequested) return _storageReady;
    _storageTrack = _currentTrack;
    _storageOnline = _onlineActive;
    if (_storageOnline) {
        _storageOnlineTrack = _onlineTrack;
        _storageOnlinePlayback = _onlinePlayback;
    }
    const Track* current = _storageTrack >= 0 ? Library.track(_storageTrack) : nullptr;
    _storagePath = current ? current->path : "";
    _storagePosition = position();
    _storageResume = _wantPlaying && hasSelection();
    _storageReady = false;
    _storageRequested = true;
    _wantPlaying = false;
    if (_state != AudioState::Error)
        _state = hasSelection() ? AudioState::Paused : AudioState::Empty;
    _command = Eject;
    if (!_taskHandle) {
        _storageReady = true;
        return true;
    }
    const uint32_t started = millis();
    while (!_storageReady && millis() - started < 2500) vTaskDelay(pdMS_TO_TICKS(2));
    return _storageReady;
}

void AudioEngine::storageEnd() {
    if (!_storageRequested) return;
    const bool resume = _storageResume;
    const bool online = _storageOnline;
    const std::string path = _storagePath;
    const uint32_t savedPosition = _storagePosition;
    const OnlineTrack onlineTrack = _storageOnlineTrack;
    const OnlinePlayback onlinePlayback = _storageOnlinePlayback;
    _storageResume = false;
    _storagePath = "";
    _storageOnline = false;
    _storageOnlineTrack = OnlineTrack();
    _storageOnlinePlayback = OnlinePlayback();
    _storageRequested = false;
    _storageReady = false;
    if (online) {
        if (resume) playOnline(onlineTrack, onlinePlayback, savedPosition);
        else {
            _currentTrack = -1;
            _onlineActive = true;
            _onlineTrack = onlineTrack;
            _onlinePlayback = onlinePlayback;
            _positionBase = savedPosition;
            _clockStart = millis();
            _wantPlaying = false;
            _state = AudioState::Paused;
            _command = None;
        }
        return;
    }
    const int track = !path.empty() ? Library.findByPath(path) : -1;
    if (track >= 0 && resume) {
        play(track, savedPosition);
    } else if (track >= 0) {
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
    const uint32_t length = duration();
    if (length && value > length) value = length;
    return value;
}

uint32_t AudioEngine::duration() const {
    if (_onlineActive) return _onlineTrack.duration;
    const Track* track = _currentTrack >= 0 ? Library.track(_currentTrack) : nullptr;
    return track ? track->duration : 0;
}

void AudioEngine::destroyDecoder() {
    if (_mp3) {
        _mp3->stop();
        delete _mp3;
        _mp3 = nullptr;
    }
    if (_netStream) {
        _netStream->close();
        delete _netStream;
        _netStream = nullptr;
    }
    if (_source) {
        delete _source;
        _source = nullptr;
    }
}

bool AudioEngine::loadTrack(int index, uint32_t startSeconds) {
    destroyDecoder();
    _onlineActive = false;
    const Track* track = Library.track(index);
    if (!track) {
        _state = AudioState::Empty;
        return false;
    }
    Lyrics.load(track->path, track->seed);
    auto* sd = new (std::nothrow) SdFileSource();
    if (!sd || !sd->open(storage_vfs_path(track->path))) {
        delete sd;
        snprintf(_error, sizeof(_error), "Cannot open MP3 file");
        printf("MP3_MEMORY_ERROR freeHeap=%u maxAlloc=%u\n", (unsigned)freeHeap(), (unsigned)maxAlloc());
        _wantPlaying = false;
        _state = AudioState::Error;
        return false;
    }
    _source = sd;
    _mp3 = new (std::nothrow) Mp3Decoder();
    if (!_mp3) {
        snprintf(_error, sizeof(_error), "Not enough memory for MP3");
        printf("MP3_MEMORY_ERROR freeHeap=%u maxAlloc=%u\n", (unsigned)freeHeap(), (unsigned)maxAlloc());
        _wantPlaying = false;
        _state = AudioState::Error;
        destroyDecoder();
        return false;
    }
    if (startSeconds && track->bitrate) {
        const uint64_t bytePosition = seekByteOffset(startSeconds, track->bitrate);
        if (bytePosition < track->fileSize) sd->seek((uint32_t)bytePosition);
    }
    if (!_mp3->begin(_source)) {
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
    printf("MP3_STARTED track=%d freeHeap=%u maxAlloc=%u audioStackHwm=%u\n",
           index, (unsigned)freeHeap(), (unsigned)maxAlloc(), (unsigned)stackHighWater());
    return true;
}

bool AudioEngine::loadOnline(uint32_t startSeconds) {
    destroyDecoder();
    _currentTrack = -1;
    _onlineActive = true;
    _onlineTrack = _pendingOnlineTrack;
    _onlinePlayback = _pendingOnlinePlayback;
    _pendingOnlineTrack = OnlineTrack();
    _pendingOnlinePlayback = OnlinePlayback();
    if (_onlinePlayback.lyric.empty())
        _onlinePlayback.lyric = Netease.cachedLyric(_onlineTrack.id);
    Lyrics.loadText(_onlinePlayback.lyric, onlineSeed());
    _onlinePlayback.lyric = "";
    if (!_onlineTrack.id || _onlinePlayback.url.empty()) {
        snprintf(_error, sizeof(_error), "Online track has no playable URL");
        _wantPlaying = false;
        _state = AudioState::Error;
        return false;
    }
    _netStream = new (std::nothrow) NetStream();
    if (!_netStream || !_netStream->open(_onlinePlayback.url, 0)) {
        snprintf(_error, sizeof(_error), "Cannot open online audio stream");
        _wantPlaying = false;
        _state = AudioState::Error;
        destroyDecoder();
        return false;
    }
    if (startSeconds && _onlinePlayback.bitrate) {
        const uint64_t bytePosition = seekByteOffset(startSeconds, _onlinePlayback.bitrate);
        if (!_onlinePlayback.size || bytePosition < _onlinePlayback.size) {
            if (!_netStream->seek((uint32_t)bytePosition)) {
                snprintf(_error, sizeof(_error), "Online audio server rejected seeking");
                _wantPlaying = false;
                _state = AudioState::Error;
                destroyDecoder();
                return false;
            }
        }
    }
    _mp3 = new (std::nothrow) Mp3Decoder();
    if (!_mp3) {
        snprintf(_error, sizeof(_error), "Not enough memory for online MP3");
        printf("MP3_MEMORY_ERROR freeHeap=%u maxAlloc=%u\n", (unsigned)freeHeap(), (unsigned)maxAlloc());
        _wantPlaying = false;
        _state = AudioState::Error;
        destroyDecoder();
        return false;
    }
    if (!_mp3->begin(_netStream)) {
        snprintf(_error, sizeof(_error), "Cannot decode online MP3");
        _wantPlaying = false;
        _state = AudioState::Error;
        destroyDecoder();
        return false;
    }
    _positionBase = startSeconds;
    _clockStart = millis();
    _wantPlaying = true;
    _state = AudioState::Playing;
    printf("NETEASE_STARTED id=%llu freeHeap=%u maxAlloc=%u audioStackHwm=%u\n",
           (unsigned long long)_onlineTrack.id, (unsigned)freeHeap(), (unsigned)maxAlloc(), (unsigned)stackHighWater());
    return true;
}

void AudioEngine::resolveOnline(int delta) {
    OnlineTrack track;
    bool found = false;
    if (delta == 0) {
        track = _onlineTrack;
        found = track.id != 0;
    } else {
        found = Netease.adjacent(_onlineTrack.id, delta, _mode == PlayMode::Shuffle, track);
    }
    if (!found) {
        snprintf(_error, sizeof(_error), "No adjacent online track; search again in WebUI");
        _wantPlaying = false;
        _state = AudioState::Error;
        return;
    }
    OnlinePlayback playback;
    std::string error;
    if (!Netease.resolve(track, playback, error)) {
        snprintf(_error, sizeof(_error), "%s", error.c_str());
        _onlineTrack = track;
        _positionBase = 0;
        _wantPlaying = false;
        _state = AudioState::Error;
        return;
    }
    _pendingOnlineTrack = track;
    _pendingOnlinePlayback = playback;
    loadOnline(0);
}

void AudioEngine::task() {
    bool decoderWasRunning = false;
    uint32_t lastReport = millis();
    int16_t pcm[512];  // 256 stereo pairs per block
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
        const Command command = _command;
        if (command != None) {
            _command = None;
            if (command == Load || command == Seek) {
                loadTrack(_requestedTrack, _requestedPosition);
                decoderWasRunning = false;
            } else if (command == LoadOnline) {
                loadOnline(_requestedPosition);
                decoderWasRunning = false;
            } else if (command == ResolveOnline) {
                resolveOnline(_requestedOnlineDelta);
                decoderWasRunning = false;
            } else if (command == Eject) {
                destroyDecoder();
                decoderWasRunning = false;
            }
        }

        if (_wantPlaying && _mp3 && _mp3->running()) {
            decoderWasRunning = true;
            const size_t pairs = _mp3->decode(pcm, 256);
            if (pairs) {
                _output->setRate(_mp3->sampleRate());
                _output->write(pcm, pairs * 2, 200);
            }
            if (!_mp3->running()) {
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
            printf("AUDIO_ALIVE source=%s pos=%lu freeHeap=%u minFreeHeap=%u audioStackHwm=%u\n",
                   _onlineActive ? "netease" : "sd", (unsigned long)position(),
                   (unsigned)freeHeap(), (unsigned)esp_get_minimum_free_heap_size(), (unsigned)stackHighWater());
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

}  // namespace lz
