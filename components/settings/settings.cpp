#include "settings.h"

#include <nvs_flash.h>
#include <cstdio>
#include <cstring>

namespace lz {

UserSettings Settings;

namespace {
// Verified without account credentials on 2026-11-18 (see original README):
// third-party public endpoint, replaceable by a self-hosted API.
constexpr char DEFAULT_NETEASE_API[] = "https://www.xlvip.ltd";

nvs_handle_t openNvs() {
    nvs_handle_t handle = 0;
    if (nvs_open("lazer", NVS_READWRITE, &handle) != ESP_OK) return 0;
    return handle;
}

uint8_t getU8(nvs_handle_t h, const char* key, uint8_t fallback) {
    uint8_t value = fallback;
    if (nvs_get_u8(h, key, &value) != ESP_OK) return fallback;
    return value;
}

bool getBool(nvs_handle_t h, const char* key, bool fallback) {
    uint8_t value = fallback ? 1 : 0;
    if (nvs_get_u8(h, key, &value) != ESP_OK) return fallback;
    return value != 0;
}

std::string getStr(nvs_handle_t h, const char* key, const char* fallback) {
    size_t len = 0;
    if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0) return fallback;
    std::string value(len - 1, '\0');
    if (nvs_get_str(h, key, value.data(), &len) != ESP_OK) return fallback;
    return value;
}

void favoriteKey(uint32_t seed, char* out, size_t cap) {
    snprintf(out, cap, "f%08lx", (unsigned long)seed);
}
}  // namespace

void UserSettings::begin() {
    nvs_handle_t h = openNvs();
    if (!h) return;
    volume = getU8(h, "volume", 12);
    if (volume > 20) volume = 12;
    brightness = getU8(h, "bright", 180);
    if (brightness < 40) brightness = 180;
    dark = getBool(h, "dark", true);
    reduceMotion = getBool(h, "reduce", false);
    showLyrics = getBool(h, "lyrics", true);
    playMode = (PlayMode)getU8(h, "mode", 0);
    if ((uint8_t)playMode > 2) playMode = PlayMode::Sequential;
    webEnabled = getBool(h, "web", true);
    staSsid = getStr(h, "stassid", "");
    staPassword = getStr(h, "stapass", "");
    apSsid = getStr(h, "apssid", "Lazer-Cardputer");
    apPassword = getStr(h, "appass", "lazer1234");
    webPassword = getStr(h, "webpass", "lazer1234");
    neteaseApi = getStr(h, "ncmapi", DEFAULT_NETEASE_API);
    if (neteaseApi.empty()) {
        // Upgrade v1.2.x installations that saved an empty API setting.
        neteaseApi = DEFAULT_NETEASE_API;
        nvs_set_str(h, "ncmapi", neteaseApi.c_str());
        nvs_commit(h);
    }
    if (apSsid.empty()) apSsid = "Lazer-Cardputer";
    if (apPassword.size() < 8) apPassword = "lazer1234";
    if (webPassword.size() < 8) webPassword = "lazer1234";
    nvs_close(h);
}

void UserSettings::save() {
    nvs_handle_t h = openNvs();
    if (!h) return;
    nvs_set_u8(h, "volume", volume);
    nvs_set_u8(h, "bright", brightness);
    nvs_set_u8(h, "dark", dark ? 1 : 0);
    nvs_set_u8(h, "reduce", reduceMotion ? 1 : 0);
    nvs_set_u8(h, "lyrics", showLyrics ? 1 : 0);
    nvs_set_u8(h, "mode", (uint8_t)playMode);
    nvs_set_u8(h, "web", webEnabled ? 1 : 0);
    nvs_set_str(h, "stassid", staSsid.c_str());
    nvs_set_str(h, "stapass", staPassword.c_str());
    nvs_set_str(h, "apssid", apSsid.c_str());
    nvs_set_str(h, "appass", apPassword.c_str());
    nvs_set_str(h, "webpass", webPassword.c_str());
    nvs_set_str(h, "ncmapi", neteaseApi.c_str());
    nvs_commit(h);
    nvs_close(h);
}

bool UserSettings::isFavorite(uint32_t seed) {
    for (const auto& entry : _favorites)
        if (entry.seed == seed) return entry.value;
    bool value = false;
    nvs_handle_t h = openNvs();
    if (h) {
        char key[13];
        favoriteKey(seed, key, sizeof(key));
        value = getBool(h, key, false);
        nvs_close(h);
    }
    _favorites.push_back({seed, value});
    return value;
}

bool UserSettings::toggleFavorite(uint32_t seed) {
    const bool value = !isFavorite(seed);
    for (auto& entry : _favorites)
        if (entry.seed == seed) entry.value = value;
    nvs_handle_t h = openNvs();
    if (h) {
        char key[13];
        favoriteKey(seed, key, sizeof(key));
        nvs_set_u8(h, key, value ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
    return value;
}

}  // namespace lz
