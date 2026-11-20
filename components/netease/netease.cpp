#include "netease.h"
#include "board.h"
#include "netease_parse.h"
#include "settings.h"
#include "strutil.h"
#include "wifi_mgr.h"

#include <cJSON.h>
#include <cstring>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_random.h>
#include <nvs_flash.h>

namespace lz {

NeteaseCloud Netease;

namespace {
constexpr size_t MAX_API_RESPONSE = 96 * 1024;
constexpr char kTag[] = "Netease";

struct RequestCapture {
    std::string setCookie;
};

esp_err_t captureEvent(esp_http_client_event_t* event) {
    auto* capture = static_cast<RequestCapture*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value) {
        if (strcasecmp(event->header_key, "Set-Cookie") == 0 && capture)
            capture->setCookie = event->header_value;
    }
    return ESP_OK;
}
}  // namespace

NeteaseCloud::NeteaseCloud() { _mutex = xSemaphoreCreateMutex(); }

bool NeteaseCloud::lock() const {
    return _mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(15000)) == pdTRUE;
}

void NeteaseCloud::unlock() const {
    if (_mutex) xSemaphoreGive(_mutex);
}

void NeteaseCloud::begin() {
    nvs_handle_t h = 0;
    if (nvs_open("lazer-ncm", NVS_READONLY, &h) != ESP_OK) return;
    auto readStr = [&](const char* key) {
        size_t len = 0;
        if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || !len) return std::string();
        std::string value(len - 1, '\0');
        if (nvs_get_str(h, key, value.data(), &len) != ESP_OK) return std::string();
        return value;
    };
    _cookie = readStr("cookie");
    _nickname = readStr("nickname");
    nvs_get_u64(h, "userid", &_userId);
    nvs_close(h);
    _loggedIn = !_cookie.empty();
    if (_loggedIn && _nickname.empty()) _nickname = "已保存登录（待验证）";
}

bool NeteaseCloud::configured() const { return !Settings.neteaseApi.empty(); }

std::string NeteaseCloud::apiUrl() const { return Settings.neteaseApi; }

bool NeteaseCloud::configure(const std::string& input, std::string& error) {
    std::string url = trim(input);
    while (!url.empty() && url.back() == '/') url.pop_back();
    if (url.size() < 10 || url.size() > 160 ||
        (!startsWith(url, "http://") && !startsWith(url, "https://")) ||
        url.find(' ') != std::string::npos || url.find('#') != std::string::npos ||
        url.find('?') != std::string::npos) {
        error = "API 地址应为 http://主机:端口，且不能包含路径参数";
        return false;
    }
    const bool changed = Settings.neteaseApi != url;
    Settings.neteaseApi = url;
    Settings.save();
    if (lock()) {
        // Never forward credentials from the previous API host to a new host.
        if (changed) clearLoginLocked();
        _lastError = "";
        unlock();
    }
    return true;
}

bool NeteaseCloud::request(const std::string& path, const std::string& parameters, bool auth,
                           bool post, std::string& response, std::string& error) {
    response.clear();
    _lastSetCookie.clear();
    if (!configured()) {
        error = "请先设置 NeteaseCloudMusicApi 地址";
        return false;
    }
    if (!wifi_sta_connected()) {
        error = "家庭 Wi-Fi 尚未连接";
        return false;
    }
    std::string url = Settings.neteaseApi + path;
    std::string body;
    if (post) body = parameters;
    else if (!parameters.empty()) url += "?" + parameters + "&timestamp=" + u64str(millis());
    else url += "?timestamp=" + u64str(millis());

    RequestCapture capture;
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    config.timeout_ms = 12000;
    config.buffer_size = 2048;
    config.event_handler = captureEvent;
    config.user_data = &capture;
    config.user_agent = "Lazer-Cardputer/2.0";
    config.max_redirection_count = 5;
    // No CA bundle: encrypts transport without authenticating the API cert,
    // matching the original setInsecure() policy (see README warnings).
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        error = "无法初始化 HTTP 请求";
        return false;
    }
    if (auth && !_cookie.empty())
        esp_http_client_set_header(client, "Cookie", _cookie.c_str());
    if (post) {
        esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
        esp_http_client_set_post_field(client, body.c_str(), (int)body.size());
    }
    esp_err_t err = esp_http_client_open(client, post ? (int)body.size() : 0);
    if (err != ESP_OK) {
        error = "API 连接失败：" + std::string(esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
    const int64_t length = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (length > (int64_t)MAX_API_RESPONSE) {
        error = "API 响应超过 96 KiB，已中止读取";
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    char buffer[1024];
    bool readOk = true;
    while (!esp_http_client_is_complete_data_received(client)) {
        const int count = esp_http_client_read(client, buffer, sizeof(buffer));
        if (count < 0) {
            error = "API 响应读取超时或不完整";
            readOk = false;
            break;
        }
        if (count == 0) break;
        if (response.size() + count > MAX_API_RESPONSE) {
            error = "API 响应超过 96 KiB，已中止读取";
            readOk = false;
            break;
        }
        response.append(buffer, count);
    }
    _lastSetCookie = capture.setCookie;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (!readOk) return false;
    if (length >= 0 && response.size() != (size_t)length) {
        error = "API 响应不完整";
        return false;
    }
    if (status != 200) {
        error = "API 返回 HTTP " + std::to_string(status);
        const std::string detail = jsonMessageOf(response.c_str(), "");
        if (!detail.empty()) error += "：" + detail;
        return false;
    }
    return true;
}

bool NeteaseCloud::saveLoginLocked(const std::string& cookie, const std::string& nickname,
                                   uint64_t userId) {
    if (cookie.empty() || cookie.size() > 3800) return false;
    nvs_handle_t h = 0;
    if (nvs_open("lazer-ncm", NVS_READWRITE, &h) != ESP_OK) return false;
    // QR check responses may omit userId; zero is valid until /login/status fills it.
    esp_err_t err = nvs_set_str(h, "cookie", cookie.c_str());
    if (err == ESP_OK) err = nvs_set_str(h, "nickname", nickname.c_str());
    nvs_set_u64(h, "userid", userId);
    nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return false;
    _cookie = cookie;
    _nickname = !nickname.empty() ? nickname : "网易云用户";
    _userId = userId;
    _loggedIn = true;
    _lastError = "";
    return true;
}

void NeteaseCloud::clearLoginLocked() {
    nvs_handle_t h = 0;
    if (nvs_open("lazer-ncm", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    _cookie.clear();
    _nickname.clear();
    _userId = 0;
    _loggedIn = false;
    _qrKey.clear();
    _qrCreated = 0;
}

bool NeteaseCloud::refreshStatus(std::string& error) {
    if (!lock()) {
        error = "另一个网易云请求尚未完成";
        return false;
    }
    std::string response;
    bool ok = request("/login/status", "", true, false, response, error);
    if (ok) {
        bool hasProfile = false;
        std::string nickname;
        uint64_t userId = 0;
        if (!parseStatusResponse(response.c_str(), hasProfile, nickname, userId)) {
            error = "登录状态响应不是有效 JSON";
            ok = false;
        } else if (hasProfile) {
            _nickname = nickname;
            _userId = userId;
            _loggedIn = !_cookie.empty();
            nvs_handle_t h = 0;
            if (nvs_open("lazer-ncm", NVS_READWRITE, &h) == ESP_OK) {
                nvs_set_str(h, "nickname", _nickname.c_str());
                nvs_set_u64(h, "userid", _userId);
                nvs_commit(h);
                nvs_close(h);
            }
        } else {
            _loggedIn = false;
            _nickname = !_cookie.empty() ? "登录已失效，请重新登录" : "";
        }
        if (ok) _lastError = "";
    }
    if (!ok) _lastError = error;
    unlock();
    return ok;
}

bool NeteaseCloud::loginPhone(const std::string& countryCode, const std::string& phone,
                              const std::string& password, std::string& error) {
    if (phone.empty() || password.empty()) {
        error = "手机号和密码不能为空";
        return false;
    }
    if (!lock()) {
        error = "另一个网易云请求尚未完成";
        return false;
    }
    const std::string parameters = "phone=" + urlEncode(phone) + "&password=" + urlEncode(password) +
        "&countrycode=" + urlEncode(!countryCode.empty() ? countryCode : "86");
    std::string response;
    bool ok = request("/login/cellphone", parameters, false, true, response, error);
    if (ok) {
        std::string cookie, nickname;
        uint64_t userId = 0;
        if (!parseLoginResponse(response.c_str(), cookie, nickname, userId, error)) {
            ok = false;
        } else {
            if (cookie.empty()) cookie = _lastSetCookie;
            if (!saveLoginLocked(cookie, nickname, userId)) {
                error = "登录成功，但 Cookie 无法保存到设备";
                ok = false;
            }
        }
    }
    if (!ok) _lastError = error;
    unlock();
    return ok;
}

bool NeteaseCloud::qrBegin(std::string& qrImage, std::string& qrUrl, std::string& error) {
    qrImage.clear();
    qrUrl.clear();
    if (!lock()) {
        error = "另一个网易云请求尚未完成";
        return false;
    }
    std::string response;
    bool ok = request("/login/qr/key", "", false, false, response, error);
    std::string key;
    if (ok) {
        cJSON* root = cJSON_Parse(response.c_str());
        if (!root) {
            error = "二维码 key 响应无效";
            ok = false;
        } else {
            const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
            const cJSON* unikey = data ? cJSON_GetObjectItemCaseSensitive(data, "unikey") : nullptr;
            if (cJSON_IsString(unikey) && unikey->valuestring) key = unikey->valuestring;
            cJSON_Delete(root);
            if (key.empty()) {
                error = "API 未返回二维码 key";
                ok = false;
            }
        }
    }
    if (ok) ok = request("/login/qr/create", "key=" + urlEncode(key) + "&qrimg=true",
                         false, false, response, error);
    if (ok) {
        cJSON* root = cJSON_Parse(response.c_str());
        if (!root) {
            error = "二维码响应无效";
            ok = false;
        } else {
            const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
            const cJSON* img = data ? cJSON_GetObjectItemCaseSensitive(data, "qrimg") : nullptr;
            const cJSON* url = data ? cJSON_GetObjectItemCaseSensitive(data, "qrurl") : nullptr;
            if (cJSON_IsString(img) && img->valuestring) qrImage = img->valuestring;
            if (cJSON_IsString(url) && url->valuestring) qrUrl = url->valuestring;
            cJSON_Delete(root);
            if (!startsWith(qrImage, "data:image/") || qrUrl.empty()) {
                error = "API 未返回完整二维码内容";
                qrImage.clear();
                qrUrl.clear();
                ok = false;
            } else {
                _qrKey = key;
                _qrCreated = millis();
            }
        }
    }
    if (!ok) _lastError = error;
    unlock();
    return ok;
}

int NeteaseCloud::qrCheck(std::string& error) {
    if (!lock()) {
        error = "另一个网易云请求尚未完成";
        return -1;
    }
    if (_qrKey.empty()) {
        error = "请先生成二维码";
        unlock();
        return -1;
    }
    if (millis() - _qrCreated > 180000) {
        _qrKey.clear();
        error = "二维码已过期，请重新生成";
        unlock();
        return 800;
    }
    std::string response;
    int result = -1;
    if (request("/login/qr/check", "key=" + urlEncode(_qrKey), false, false, response, error)) {
        std::string cookie, nickname;
        uint64_t userId = 0;
        result = parseQrCheckResponse(response.c_str(), cookie, nickname, userId, error);
        if (result == 803) {
            if (cookie.empty()) cookie = _lastSetCookie;
            if (!saveLoginLocked(cookie, nickname, userId)) {
                error = "扫码成功，但 Cookie 无法保存到设备";
                result = -1;
            } else {
                _qrKey.clear();
            }
        } else if (result == 800) {
            _qrKey.clear();
        }
    }
    if (result < 0 && !error.empty()) _lastError = error;
    else if (result == 803) _lastError = "";
    unlock();
    return result;
}

bool NeteaseCloud::logout(std::string& error) {
    if (!lock()) {
        error = "另一个网易云请求尚未完成";
        return false;
    }
    std::string response, remoteError;
    request("/logout", "", true, true, response, remoteError);
    clearLoginLocked();
    _lastError = "";
    unlock();
    return true;
}

bool NeteaseCloud::search(const std::string& input, std::string& error) {
    const std::string keywords = trim(input);
    if (keywords.empty() || keywords.size() > 100) {
        error = "搜索关键词应为 1–100 字节";
        return false;
    }
    if (!lock()) {
        error = "另一个网易云请求尚未完成";
        return false;
    }
    std::string response;
    bool ok = request("/cloudsearch", "keywords=" + urlEncode(keywords) + "&type=1&limit=8",
                      true, false, response, error);
    if (ok) ok = parseSearchResponse(response.c_str(), _results, error);
    _lastError = ok ? "" : error;
    unlock();
    return ok;
}

bool NeteaseCloud::resultById(uint64_t id, OnlineTrack& track) const {
    if (!lock()) return false;
    bool found = false;
    for (const auto& result : _results) {
        if (result.id == id) {
            track = result;
            found = true;
            break;
        }
    }
    unlock();
    return found;
}

bool NeteaseCloud::adjacent(uint64_t currentId, int delta, bool shuffle, OnlineTrack& track) const {
    if (!lock()) return false;
    if (_results.empty()) {
        unlock();
        return false;
    }
    int current = -1;
    for (size_t i = 0; i < _results.size(); ++i) {
        if (_results[i].id == currentId) {
            current = (int)i;
            break;
        }
    }
    if (current < 0) {
        unlock();
        return false;
    }
    int next = current;
    if (shuffle && _results.size() > 1) {
        next = (int)(esp_random() % (uint32_t)(_results.size() - 1));
        if (next >= current) ++next;
    } else {
        next = (current + delta + (int)_results.size()) % (int)_results.size();
    }
    track = _results[next];
    unlock();
    return true;
}

bool NeteaseCloud::lyricLocked(uint64_t id, std::string& lyric) {
    if (_lyricId == id) {
        lyric = _lyricCache;
        return true;
    }
    std::string response, error;
    if (!request("/lyric", "id=" + u64str(id), true, false, response, error)) return false;
    cJSON* root = cJSON_Parse(response.c_str());
    if (!root) return false;
    const cJSON* lrc = cJSON_GetObjectItemCaseSensitive(root, "lrc");
    const cJSON* text = lrc ? cJSON_GetObjectItemCaseSensitive(lrc, "lyric") : nullptr;
    lyric = (cJSON_IsString(text) && text->valuestring) ? text->valuestring : "";
    cJSON_Delete(root);
    if (lyric.size() > 48 * 1024) lyric.clear();
    _lyricId = id;
    _lyricCache = lyric;
    return true;
}

std::string NeteaseCloud::cachedLyric(uint64_t id) {
    if (!id || !lock()) return "";
    std::string lyric;
    lyricLocked(id, lyric);
    unlock();
    return lyric;
}

bool NeteaseCloud::resolve(const OnlineTrack& track, OnlinePlayback& playback, std::string& error) {
    if (!track.id) {
        error = "歌曲 ID 无效";
        return false;
    }
    if (!lock()) {
        error = "另一个网易云请求尚未完成";
        return false;
    }
    std::string response;
    bool ok = request("/song/url/v1", "id=" + u64str(track.id) + "&level=standard",
                      true, false, response, error);
    if (ok) ok = parseResolveResponse(response.c_str(), playback, error);
    if (ok) {
        // Lyrics are optional. A lyric failure must not block otherwise valid audio.
        lyricLocked(track.id, playback.lyric);
    }
    _lastError = ok ? "" : error;
    unlock();
    return ok;
}

}  // namespace lz
