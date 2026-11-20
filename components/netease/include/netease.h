#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "netease_types.h"

namespace lz {

// Port of NeteaseCloud: esp_http_client + cJSON against a configurable
// NeteaseCloudMusicApi. Login cookies live in NVS namespace "lazer-ncm";
// passwords are never persisted.
class NeteaseCloud {
public:
    NeteaseCloud();
    void begin();

    bool configure(const std::string& url, std::string& error);
    bool configured() const;
    std::string apiUrl() const;
    bool loggedIn() const { return _loggedIn; }
    std::string nickname() const { return _nickname; }
    std::string lastError() const { return _lastError; }

    bool refreshStatus(std::string& error);
    bool loginPhone(const std::string& countryCode, const std::string& phone,
                    const std::string& password, std::string& error);
    bool qrBegin(std::string& qrImage, std::string& qrUrl, std::string& error);
    int qrCheck(std::string& error);
    bool logout(std::string& error);

    bool search(const std::string& keywords, std::string& error);
    const std::vector<OnlineTrack>& results() const { return _results; }
    bool resultById(uint64_t id, OnlineTrack& track) const;
    bool adjacent(uint64_t currentId, int delta, bool shuffle, OnlineTrack& track) const;
    bool resolve(const OnlineTrack& track, OnlinePlayback& playback, std::string& error);
    std::string cachedLyric(uint64_t id);

private:
    bool request(const std::string& path, const std::string& parameters, bool auth,
                 bool post, std::string& response, std::string& error);
    bool lyricLocked(uint64_t id, std::string& lyric);
    bool saveLoginLocked(const std::string& cookie, const std::string& nickname,
                         uint64_t userId);
    void clearLoginLocked();
    bool lock() const;
    void unlock() const;

    SemaphoreHandle_t _mutex;
    std::string _cookie;
    std::string _nickname;
    std::string _lastError;
    std::string _lastSetCookie;
    uint64_t _userId = 0;
    bool _loggedIn = false;
    std::string _qrKey;
    uint32_t _qrCreated = 0;
    std::vector<OnlineTrack> _results;
    uint64_t _lyricId = 0;
    std::string _lyricCache;
};

extern NeteaseCloud Netease;

}  // namespace lz
