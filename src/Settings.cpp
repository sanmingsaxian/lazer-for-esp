#include "Settings.h"

UserSettings Settings;

void UserSettings::begin() {
    _preferences.begin("lazer", false);
    volume = _preferences.getUChar("volume", 12);
    if (volume > 20) volume = 12;
    brightness = _preferences.getUChar("bright", 180);
    if (brightness < 40) brightness = 180;
    dark = _preferences.getBool("dark", true);
    reduceMotion = _preferences.getBool("reduce", false);
    showLyrics = _preferences.getBool("lyrics", true);
    playMode = (PlayMode)_preferences.getUChar("mode", 0);
    if ((uint8_t)playMode > 2) playMode = PlayMode::Sequential;
}

void UserSettings::save() {
    _preferences.putUChar("volume", volume);
    _preferences.putUChar("bright", brightness);
    _preferences.putBool("dark", dark);
    _preferences.putBool("reduce", reduceMotion);
    _preferences.putBool("lyrics", showLyrics);
    _preferences.putUChar("mode", (uint8_t)playMode);
}

bool UserSettings::isFavorite(uint32_t seed) {
    char key[13];
    snprintf(key, sizeof(key), "f%08lx", (unsigned long)seed);
    for (const auto& entry : _favorites) {
        if (entry.seed == seed) return entry.value;
    }
    bool value = _preferences.getBool(key, false);
    _favorites.push_back({seed, value});
    return value;
}

bool UserSettings::toggleFavorite(uint32_t seed) {
    bool value = !isFavorite(seed);
    for (auto& entry : _favorites) {
        if (entry.seed == seed) entry.value = value;
    }
    char key[13];
    snprintf(key, sizeof(key), "f%08lx", (unsigned long)seed);
    _preferences.putBool(key, value);
    return value;
}
