#pragma once
#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "audio_out.h"
#include "library.h"
#include "mp3dec.h"
#include "net_stream.h"
#include "netease_types.h"
#include "queue.h"

namespace lz {

enum class AudioState : uint8_t { Empty, Loading, Playing, Paused, Error };

// Port of the original AudioEngine: command-driven decode task on Core 1
// (priority 4, 20 KiB stack), SD/online sources, storage互斥 pause/resume.
class AudioEngine {
public:
    bool begin();
    void play(int index, uint32_t startSeconds = 0);
    void playOnline(const OnlineTrack& track, const OnlinePlayback& playback,
                    uint32_t startSeconds = 0);
    void eject();
    void toggle();
    void pause();
    void resume();
    void next(bool automatic = false);
    void previous();
    void seek(int deltaSeconds);
    bool storageBegin();
    void storageEnd();
    void setVolume(uint8_t volume);
    void setPlayMode(PlayMode mode) { _mode = mode; }

    int currentTrack() const { return _currentTrack; }
    bool onlineActive() const { return _onlineActive; }
    bool hasSelection() const { return _currentTrack >= 0 || _onlineActive; }
    uint64_t onlineId() const { return _onlineTrack.id; }
    std::string onlineTitle() const { return _onlineTrack.title; }
    std::string onlineArtist() const { return _onlineTrack.artist; }
    std::string onlineAlbum() const { return _onlineTrack.album; }
    uint32_t onlineSeed() const;
    uint32_t position() const;
    uint32_t duration() const;
    AudioState state() const { return _state; }
    bool playing() const { return _wantPlaying; }
    PlayMode playMode() const { return _mode; }
    const char* error() const { return _error; }
    uint32_t stackHighWater() const;

private:
    enum Command : uint8_t { None, Load, Seek, Eject, LoadOnline, ResolveOnline };
    static void taskEntry(void* argument);
    void task();
    bool loadTrack(int index, uint32_t startSeconds);
    bool loadOnline(uint32_t startSeconds);
    void resolveOnline(int delta);
    void reloadOnline(uint32_t startSeconds = 0);
    void destroyDecoder();
    int nextIndex(bool automatic) const;

    IMp3Source* _source = nullptr;   // SdFileSource (owned)
    NetStream* _netStream = nullptr;
    Mp3Decoder* _mp3 = nullptr;
    AudioOutput* _output = nullptr;
    TaskHandle_t _taskHandle = nullptr;

    volatile Command _command = None;
    volatile int _requestedTrack = -1;
    volatile uint32_t _requestedPosition = 0;
    volatile int _requestedOnlineDelta = 1;
    volatile int _currentTrack = -1;
    volatile bool _onlineActive = false;
    volatile AudioState _state = AudioState::Empty;
    volatile bool _wantPlaying = false;
    volatile bool _storageRequested = false;
    volatile bool _storageReady = false;
    int _storageTrack = -1;
    std::string _storagePath;
    uint32_t _storagePosition = 0;
    bool _storageResume = false;
    bool _storageOnline = false;
    OnlineTrack _storageOnlineTrack;
    OnlinePlayback _storageOnlinePlayback;
    OnlineTrack _onlineTrack;
    OnlinePlayback _onlinePlayback;
    OnlineTrack _pendingOnlineTrack;
    OnlinePlayback _pendingOnlinePlayback;
    volatile PlayMode _mode = PlayMode::Sequential;
    volatile uint32_t _positionBase = 0;
    volatile uint32_t _clockStart = 0;
    uint8_t _volume = 12;
    char _error[96] = {0};
};

extern AudioEngine Audio;

}  // namespace lz
