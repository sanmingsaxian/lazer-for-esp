#pragma once

#include <Arduino.h>
#include <M5Cardputer.h>
#include <vector>
#include "AudioEngine.h"
#include "Library.h"
#include "Settings.h"

enum class Screen : uint8_t { Home, Library, Favorites, Settings };

class LazerApp {
public:
    void begin();
    void update();

    Screen screen = Screen::Home;
    int selected = 0;
    int settingsCursor = 0;
    bool playerOpen = false;
    bool lyricsMode = true;
    bool queueOpen = false;
    bool helpOpen = false;
    String toast;
    uint32_t toastUntil = 0;

    std::vector<int> favoriteIndices() const;
    int listCount() const;
    int selectedTrack() const;
    void notify(const String& message, uint16_t milliseconds = 1800);
    void cyclePlayMode();

private:
    void handleKey(const Keyboard_Class::KeysState& keys);
    void handleBrowserKey(const Keyboard_Class::KeysState& keys, char key);
    void handlePlayerKey(const Keyboard_Class::KeysState& keys, char key);
    void handleQueueKey(const Keyboard_Class::KeysState& keys, char key);
    void handleSettingsKey(const Keyboard_Class::KeysState& keys, char key);
    void selectRelative(int delta);
    void playSelected();
    void toggleSelectedFavorite();
    void changeVolume(int delta);
    void rescan();

    uint32_t _lastBatteryRead = 0;
};

extern LazerApp App;
