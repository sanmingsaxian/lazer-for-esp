#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <vector>
#include "AudioEngine.h"

struct FavoriteEntry {
    uint32_t seed;
    bool value;
};

class UserSettings {
public:
    void begin();
    void save();

    uint8_t volume = 12;
    uint8_t brightness = 180;
    bool dark = true;
    bool reduceMotion = false;
    bool showLyrics = true;
    PlayMode playMode = PlayMode::Sequential;

    bool isFavorite(uint32_t seed);
    bool toggleFavorite(uint32_t seed);

private:
    Preferences _preferences;
    std::vector<FavoriteEntry> _favorites;
};

extern UserSettings Settings;
