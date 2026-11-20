#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "queue.h"

namespace lz {

struct FavoriteEntry {
    uint32_t seed;
    bool value;
};

// NVS-backed settings; namespace and key names are byte-compatible with the
// Arduino Preferences layout so OTA upgrades keep user data.
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
    bool webEnabled = true;
    std::string staSsid;
    std::string staPassword;
    std::string apSsid = "Lazer-Cardputer";
    std::string apPassword = "lazer1234";
    std::string webPassword = "lazer1234";
    std::string neteaseApi;

    bool isFavorite(uint32_t seed);
    bool toggleFavorite(uint32_t seed);

private:
    std::vector<FavoriteEntry> _favorites;
};

extern UserSettings Settings;

}  // namespace lz
