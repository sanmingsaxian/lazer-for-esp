#pragma once

#include <Arduino.h>
#include <AudioFileSourceID3.h>
#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "Library.h"

enum class AudioState : uint8_t { Empty, Loading, Playing, Paused, Error };
enum class PlayMode : uint8_t { Sequential, RepeatOne, Shuffle };

class AudioEngine {
public:
    bool begin();
    void play(int index, uint32_t startSeconds = 0);
    void eject();
    void toggle();
    void pause();
    void resume();
    void next(bool automatic = false);
    void previous();
    void seek(int deltaSeconds);
    void setVolume(uint8_t volume);
    void setPlayMode(PlayMode mode) { _mode = mode; }

    int currentTrack() const { return _currentTrack; }
    uint32_t position() const;
    uint32_t duration() const;
    AudioState state() const { return _state; }
    bool playing() const { return _wantPlaying; }
    PlayMode playMode() const { return _mode; }
    const char* error() const { return _error; }

private:
    enum Command : uint8_t { None, Load, Seek, Eject };
    static void taskEntry(void* argument);
    void task();
    bool loadTrack(int index, uint32_t startSeconds);
    void destroyDecoder();
    int nextIndex(bool automatic) const;

    AudioFileSourceSD* _source = nullptr;
    AudioFileSourceID3* _id3 = nullptr;
    AudioGeneratorMP3* _mp3 = nullptr;
    AudioOutputI2S* _output = nullptr;
    TaskHandle_t _taskHandle = nullptr;

    volatile Command _command = None;
    volatile int _requestedTrack = -1;
    volatile uint32_t _requestedPosition = 0;
    volatile int _currentTrack = -1;
    volatile AudioState _state = AudioState::Empty;
    volatile bool _wantPlaying = false;
    volatile PlayMode _mode = PlayMode::Sequential;
    volatile uint32_t _positionBase = 0;
    volatile uint32_t _clockStart = 0;
    uint8_t _volume = 12;
    char _error[48] = {0};
};

extern AudioEngine Audio;
