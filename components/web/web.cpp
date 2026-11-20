#include "web_internal.h"
#include "app.h"
#include "board.h"
#include "library.h"
#include "multipart.h"
#include "netease.h"
#include "pathutil.h"
#include "settings.h"
#include "storage.h"
#include "strutil.h"
#include "wifi_mgr.h"

#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>

namespace lz {

WebControl Web;

namespace {
constexpr char kTag[] = "Web";
constexpr size_t MAX_FORM_BODY = 8 * 1024;

const char* statusLine(int status) {
    switch (status) {
        case 200: return "200 OK";
        case 400: return "400 Bad Request";
        case 401: return "401 Unauthorized";
        case 404: return "404 Not Found";
        case 409: return "409 Conflict";
        case 413: return "413 Payload Too Large";
        case 500: return "500 Internal Server Error";
        case 502: return "502 Bad Gateway";
        case 503: return "503 Service Unavailable";
        default: return "500 Internal Server Error";
    }
}

uint32_t freeHeap() { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT); }
uint32_t maxAlloc() { return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); }

std::string boolStr(bool value) { return value ? "true" : "false"; }
}  // namespace

// ---------------------------------------------------------------- helpers

bool FormArgs::has(const std::string& key) const {
    for (const auto& item : items)
        if (item.first == key) return true;
    return false;
}

std::string FormArgs::get(const std::string& key) const {
    for (const auto& item : items)
        if (item.first == key) return item.second;
    return "";
}

FormArgs FormArgs::parse(const std::string& body) {
    FormArgs form;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t amp = body.find('&', pos);
        if (amp == std::string::npos) amp = body.size();
        const std::string pair = body.substr(pos, amp - pos);
        pos = amp + 1;
        if (pair.empty()) continue;
        const size_t eq = pair.find('=');
        if (eq == std::string::npos)
            form.items.push_back({urlDecode(pair), ""});
        else
            form.items.push_back({urlDecode(pair.substr(0, eq)), urlDecode(pair.substr(eq + 1))});
    }
    return form;
}

std::string queryArg(httpd_req_t* req, const char* key, bool* present) {
    if (present) *present = false;
    const size_t len = httpd_req_get_url_query_len(req);
    if (!len || len > 1024) return "";
    std::string query(len + 1, '\0');
    if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) return "";
    char value[512];
    if (httpd_query_key_value(query.c_str(), key, value, sizeof(value)) != ESP_OK) return "";
    if (present) *present = true;
    return urlDecode(value);
}

std::string readBody(httpd_req_t* req, size_t cap) {
    std::string body;
    size_t remaining = req->content_len ? req->content_len : cap;
    if (remaining > cap) remaining = cap;
    char chunk[1024];
    while (body.size() < remaining) {
        const size_t wanted = remaining - body.size() < sizeof(chunk)
            ? remaining - body.size() : sizeof(chunk);
        const int received = httpd_req_recv(req, chunk, wanted);
        if (received <= 0) break;
        body.append(chunk, received);
    }
    return body;
}

std::string arg(httpd_req_t* req, const FormArgs& form, const char* key) {
    bool present = false;
    const std::string value = queryArg(req, key, &present);
    if (present) return value;
    return form.get(key);
}

bool argBool(const std::string& value) {
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

bool authorize(WebControl* web, httpd_req_t* req) {
    char header[256] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) == ESP_OK &&
        strncasecmp(header, "Basic ", 6) == 0) {
        unsigned char decoded[192];
        size_t decodedLen = 0;
        const std::string expected = "admin:" + web->password();
        if (mbedtls_base64_decode(decoded, sizeof(decoded), &decodedLen,
                                  (const unsigned char*)header + 6, strlen(header + 6)) == 0 &&
            decodedLen == expected.size() &&
            memcmp(decoded, expected.data(), decodedLen) == 0) {
            return true;
        }
    }
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Lazer Cardputer\"");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"请输入 WebUI 管理密码\"}");
    return false;
}

esp_err_t sendJson(httpd_req_t* req, int status, const std::string& json) {
    httpd_resp_set_status(req, statusLine(status));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json.c_str(), (ssize_t)json.size());
}

esp_err_t sendText(httpd_req_t* req, int status, const char* type, const std::string& body) {
    httpd_resp_set_status(req, statusLine(status));
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body.c_str(), (ssize_t)body.size());
}

std::string jsonError(const std::string& message) {
    return "{\"ok\":false,\"error\":" + jsonQuote(message) + "}";
}

const char* audioStateName(AudioState state) {
    switch (state) {
        case AudioState::Loading: return "loading";
        case AudioState::Playing: return "playing";
        case AudioState::Paused: return "paused";
        case AudioState::Error: return "error";
        default: return "empty";
    }
}

// ---------------------------------------------------------------- lifecycle

void WebControl::scheduleReboot(uint32_t delayMs) { _rebootAt = millis() + delayMs; }
void WebControl::scheduleWifiConnect(uint32_t delayMs) { _wifiConnectAt = millis() + delayMs; }

void WebControl::cacheSDCapacity() {
    if (Library.ready()) storage_capacity(_sdTotal, _sdUsed);
    else { _sdTotal = 0; _sdUsed = 0; }
}

void WebControl::refreshLibrary() {
    if (Library.ready()) Library.rescan();
    else Library.begin();
    cacheSDCapacity();
    App.selected = 0;
}

std::string WebControl::address() const {
    if (!_running) return "关闭";
    if (wifi_sta_connected() && wifi_sta_ip() != "0.0.0.0") return wifi_sta_ip();
    return wifi_ap_ip();
}

std::string WebControl::connectionLabel() const {
    if (!_running) return "WebUI 关闭";
    return std::string(wifi_sta_connected() ? "STA " : "AP ") + address();
}

void WebControl::update() {
    if (_rebootAt && millis() >= _rebootAt) {
        _rebootAt = 0;
        vTaskDelay(pdMS_TO_TICKS(80));
        esp_restart();
    }
    if (_wifiConnectAt && millis() >= _wifiConnectAt) {
        _wifiConnectAt = 0;
        wifi_disconnect_sta();
        vTaskDelay(pdMS_TO_TICKS(120));
        wifi_connect_now();
        ESP_LOGI(kTag, "Wi-Fi STA connecting to %s", Settings.staSsid.c_str());
    }
    if (!_running) return;
    wifi_retry_if_needed();
}

// ---------------------------------------------------------------- handlers

namespace {

esp_err_t handleRoot(httpd_req_t* req, WebControl* web) {
    extern const uint8_t index_html_start[] asm("_binary_index_html_start");
    extern const uint8_t index_html_end[] asm("_binary_index_html_end");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                       "connect-src 'self'; img-src 'self' data:;");
    return httpd_resp_send(req, (const char*)index_html_start,
                           index_html_end - index_html_start);
}

esp_err_t handleState(httpd_req_t* req, WebControl* web) {
    const Track* track = Audio.currentTrack() >= 0 ? Library.track(Audio.currentTrack()) : nullptr;
    std::string out;
    out.reserve(2200);
    out = "{";
    out += "\"ok\":true,\"version\":" + jsonQuote(LAZER_VERSION);
    out += ",\"device\":\"Cardputer v1.1 / Stamp-S3A\"";
    out += ",\"playing\":" + boolStr(Audio.playing());
    out += ",\"state\":" + jsonQuote(audioStateName(Audio.state()));
    out += ",\"trackIndex\":" + std::to_string(Audio.currentTrack());
    out += ",\"title\":" + jsonQuote(track ? track->title : (Audio.onlineActive() ? Audio.onlineTitle() : "未选择曲目"));
    out += ",\"artist\":" + jsonQuote(track ? track->artist : (Audio.onlineActive() ? Audio.onlineArtist() : "Lazer Cardputer"));
    out += ",\"album\":" + jsonQuote(track ? track->album : (Audio.onlineActive() ? Audio.onlineAlbum() : "本地音乐"));
    out += ",\"path\":" + jsonQuote(track ? track->path : "");
    out += ",\"online\":" + boolStr(Audio.onlineActive());
    out += ",\"onlineId\":" + jsonQuote(Audio.onlineActive() ? u64str(Audio.onlineId()) : "");
    out += ",\"position\":" + std::to_string(Audio.position());
    out += ",\"duration\":" + std::to_string(Audio.duration());
    out += ",\"volume\":" + std::to_string(Settings.volume);
    out += ",\"brightness\":" + std::to_string(Settings.brightness);
    out += ",\"dark\":" + boolStr(Settings.dark);
    out += ",\"lyrics\":" + boolStr(Settings.showLyrics);
    out += ",\"reduceMotion\":" + boolStr(Settings.reduceMotion);
    out += ",\"mode\":" + std::to_string((uint8_t)Settings.playMode);
    out += ",\"error\":" + jsonQuote(Audio.error());
    out += ",\"sdReady\":" + boolStr(Library.ready());
    out += ",\"trackCount\":" + std::to_string(Library.count());
    out += ",\"sdTotal\":" + u64str(web->sdTotal());
    out += ",\"sdUsed\":" + u64str(web->sdUsed());
    out += ",\"apSsid\":" + jsonQuote(Settings.apSsid);
    out += ",\"staSsid\":" + jsonQuote(Settings.staSsid);
    out += ",\"apIp\":" + jsonQuote(wifi_ap_ip());
    out += ",\"staIp\":" + jsonQuote(wifi_sta_connected() ? wifi_sta_ip() : "");
    out += ",\"connected\":" + boolStr(wifi_sta_connected());
    out += ",\"rssi\":" + std::to_string(wifi_sta_connected() ? wifi_rssi() : 0);
    out += ",\"wifiStatus\":" + std::to_string(wifi_status_code());
    out += ",\"neteaseApi\":" + jsonQuote(Netease.apiUrl());
    out += ",\"neteaseLoggedIn\":" + boolStr(Netease.loggedIn());
    out += ",\"neteaseNickname\":" + jsonQuote(Netease.nickname());
    out += ",\"neteaseError\":" + jsonQuote(Netease.lastError());
    out += ",\"freeHeap\":" + std::to_string(freeHeap());
    out += ",\"minFreeHeap\":" + std::to_string((uint32_t)esp_get_minimum_free_heap_size());
    out += ",\"maxAllocHeap\":" + std::to_string(maxAlloc());
    out += ",\"audioStackHwm\":" + std::to_string(Audio.stackHighWater());
    out += ",\"bootReset\":" + jsonQuote(App.bootReset);
    out += ",\"bootResetCode\":" + std::to_string(App.bootResetCode);
    out += ",\"uptime\":" + std::to_string(millis() / 1000);
    out += "}";
    return sendJson(req, 200, out);
}

esp_err_t handleTracks(httpd_req_t* req, WebControl* web) {
    std::string out = "{\"ok\":true,\"tracks\":[";
    out.reserve(8192);
    for (size_t i = 0; i < Library.count(); ++i) {
        const Track* track = Library.track(i);
        if (i) out += ',';
        out += "{\"index\":" + std::to_string(i);
        out += ",\"title\":" + jsonQuote(track->title);
        out += ",\"artist\":" + jsonQuote(track->artist);
        out += ",\"album\":" + jsonQuote(track->album);
        out += ",\"path\":" + jsonQuote(track->path);
        out += ",\"duration\":" + std::to_string(track->duration);
        out += ",\"size\":" + std::to_string(track->fileSize);
        out += ",\"favorite\":" + boolStr(Settings.isFavorite(track->seed));
        out += "}";
    }
    out += "]}";
    return sendJson(req, 200, out);
}

esp_err_t handleControl(httpd_req_t* req, WebControl* web) {
    const FormArgs form = FormArgs::parse(readBody(req, MAX_FORM_BODY));
    const std::string action = arg(req, form, "action");
    if (action == "toggle") Audio.toggle();
    else if (action == "next") Audio.next();
    else if (action == "previous") Audio.previous();
    else if (action == "stop") Audio.eject();
    else if (action == "play") {
        const int index = atoi(arg(req, form, "index").c_str());
        if (!Library.track(index)) return sendJson(req, 400, "{\"ok\":false,\"error\":\"曲目不存在\"}");
        Audio.play(index);
        App.playerOpen = true;
    } else if (action == "seek") {
        if (!Audio.hasSelection()) return sendJson(req, 400, "{\"ok\":false,\"error\":\"尚未选择曲目\"}");
        int target = atoi(arg(req, form, "seconds").c_str());
        if (target < 0) target = 0;
        if (target > 86400) target = 86400;
        const uint32_t duration = Audio.duration();
        if (duration && (uint32_t)target >= duration) target = duration > 1 ? duration - 1 : 0;
        Audio.seek(target - (int)Audio.position());
    } else if (action == "mode") {
        const int mode = atoi(arg(req, form, "value").c_str());
        if (mode < 0 || mode > 2) return sendJson(req, 400, "{\"ok\":false,\"error\":\"播放模式无效\"}");
        Settings.playMode = (PlayMode)mode;
        Audio.setPlayMode(Settings.playMode);
        Settings.save();
    } else if (action == "favorite") {
        int index = Audio.currentTrack();
        bool hasIndex = false;
        queryArg(req, "index", &hasIndex);
        if (hasIndex || form.has("index")) index = atoi(arg(req, form, "index").c_str());
        const Track* track = Library.track(index);
        if (!track) return sendJson(req, 400, "{\"ok\":false,\"error\":\"曲目不存在\"}");
        Settings.toggleFavorite(track->seed);
    } else {
        return sendJson(req, 400, "{\"ok\":false,\"error\":\"未知控制命令\"}");
    }
    return handleState(req, web);
}

esp_err_t handleSettings(httpd_req_t* req, WebControl* web) {
    const FormArgs form = FormArgs::parse(readBody(req, MAX_FORM_BODY));
    if (form.has("volume") || queryArg(req, "volume", nullptr).size()) {
        int value = atoi(arg(req, form, "volume").c_str());
        if (value < 0) value = 0;
        if (value > 20) value = 20;
        Settings.volume = (uint8_t)value;
        Audio.setVolume(Settings.volume);
    }
    if (form.has("brightness")) {
        int value = atoi(arg(req, form, "brightness").c_str());
        if (value < 40) value = 40;
        if (value > 255) value = 255;
        Settings.brightness = (uint8_t)value;
        board_set_brightness(Settings.brightness);
    }
    if (form.has("dark")) Settings.dark = argBool(arg(req, form, "dark"));
    if (form.has("lyrics")) {
        Settings.showLyrics = argBool(arg(req, form, "lyrics"));
        App.lyricsMode = Settings.showLyrics;
    }
    if (form.has("reduceMotion")) Settings.reduceMotion = argBool(arg(req, form, "reduceMotion"));
    if (form.has("mode")) {
        int mode = atoi(arg(req, form, "mode").c_str());
        if (mode < 0) mode = 0;
        if (mode > 2) mode = 2;
        Settings.playMode = (PlayMode)mode;
        Audio.setPlayMode(Settings.playMode);
    }
    Settings.save();
    return handleState(req, web);
}

esp_err_t handleNetworkSettings(httpd_req_t* req, WebControl* web) {
    const FormArgs form = FormArgs::parse(readBody(req, MAX_FORM_BODY));
    std::string apSsid = trim(arg(req, form, "apSsid"));
    const std::string apPassword = arg(req, form, "apPassword");
    const std::string webPassword = arg(req, form, "webPassword");
    const std::string staSsid = arg(req, form, "staSsid");
    const std::string staPassword = arg(req, form, "staPassword");
    if (apSsid.empty() || apSsid.size() > 32 || staSsid.size() > 32 ||
        staPassword.size() > 63 || apPassword.size() > 63 || webPassword.size() > 63 ||
        (!apPassword.empty() && apPassword.size() < 8) ||
        (!webPassword.empty() && webPassword.size() < 8)) {
        return sendJson(req, 400, "{\"ok\":false,\"error\":\"名称或密码长度无效；新密码至少 8 位\"}");
    }
    const bool sameStation = staSsid == Settings.staSsid;
    Settings.staSsid = trim(staSsid);
    if (Settings.staSsid.empty()) Settings.staPassword = "";
    else if (!staPassword.empty() || !sameStation) Settings.staPassword = staPassword;
    Settings.apSsid = apSsid;
    if (!apPassword.empty()) Settings.apPassword = apPassword;
    if (!webPassword.empty()) Settings.webPassword = webPassword;
    Settings.webEnabled = true;
    Settings.save();
    // The current session keeps the old password until reboot so the reboot
    // action cannot be locked out by its own save.
    return sendJson(req, 200, "{\"ok\":true,\"rebootRequired\":true}");
}

esp_err_t handleWifiScan(httpd_req_t* req, WebControl* web) {
    std::vector<WifiScanEntry> networks;
    if (!wifi_scan(networks)) return sendJson(req, 502, jsonError("Wi-Fi 扫描失败，请稍后重试"));
    std::string out = "{\"ok\":true,\"networks\":[";
    out.reserve(1800);
    for (size_t i = 0; i < networks.size(); ++i) {
        if (i) out += ',';
        out += "{\"ssid\":" + jsonQuote(networks[i].ssid);
        out += ",\"rssi\":" + std::to_string(networks[i].rssi);
        out += ",\"secure\":" + boolStr(networks[i].secure) + "}";
    }
    out += "]}";
    return sendJson(req, 200, out);
}

esp_err_t handleWifiConnect(httpd_req_t* req, WebControl* web) {
    const FormArgs form = FormArgs::parse(readBody(req, MAX_FORM_BODY));
    const std::string ssid = trim(arg(req, form, "staSsid"));
    const std::string password = arg(req, form, "staPassword");
    if (ssid.empty() || ssid.size() > 32 || password.size() > 63 ||
        (!password.empty() && password.size() < 8)) {
        return sendJson(req, 400, jsonError("Wi-Fi 名称无效；密码应留空或为 8–63 位"));
    }
    const bool same = ssid == Settings.staSsid;
    Settings.staSsid = ssid;
    if (!password.empty() || !same) Settings.staPassword = password;
    Settings.save();
    web->scheduleWifiConnect(450);
    return sendJson(req, 200, "{\"ok\":true,\"connecting\":true}");
}

esp_err_t handleNeteaseConfig(httpd_req_t* req, WebControl* web) {
    const FormArgs form = FormArgs::parse(readBody(req, MAX_FORM_BODY));
    std::string error;
    if (!Netease.configure(arg(req, form, "api"), error)) return sendJson(req, 400, jsonError(error));
    return handleState(req, web);
}

esp_err_t handleNeteaseStatus(httpd_req_t* req, WebControl* web) {
    std::string error;
    if (!Netease.refreshStatus(error)) return sendJson(req, 502, jsonError(error));
    return handleState(req, web);
}

esp_err_t handleNeteaseQrStart(httpd_req_t* req, WebControl* web) {
    std::string image, url, error;
    if (!Netease.qrBegin(image, url, error)) return sendJson(req, 502, jsonError(error));
    return sendJson(req, 200, "{\"ok\":true,\"qrImage\":" + jsonQuote(image) + "}");
}

esp_err_t handleNeteaseQrCheck(httpd_req_t* req, WebControl* web) {
    std::string error;
    const int code = Netease.qrCheck(error);
    if (code < 0) return sendJson(req, 502, jsonError(error));
    std::string message = "请使用网易云音乐 App 扫码";
    if (code == 802) message = "已扫码，请在手机上确认登录";
    else if (code == 803) message = "登录成功";
    else if (code == 800) message = "二维码已过期，请重新生成";
    return sendJson(req, 200, "{\"ok\":true,\"code\":" + std::to_string(code) +
                              ",\"message\":" + jsonQuote(message) + "}");
}

esp_err_t handleNeteaseLogin(httpd_req_t* req, WebControl* web) {
    const FormArgs form = FormArgs::parse(readBody(req, MAX_FORM_BODY));
    std::string error;
    if (!Netease.loginPhone(arg(req, form, "countryCode"), arg(req, form, "phone"),
                            arg(req, form, "password"), error))
        return sendJson(req, 502, jsonError(error));
    return handleState(req, web);
}

esp_err_t handleNeteaseLogout(httpd_req_t* req, WebControl* web) {
    std::string error;
    Netease.logout(error);
    return handleState(req, web);
}

esp_err_t handleNeteaseSearch(httpd_req_t* req, WebControl* web) {
    std::string error;
    if (!Netease.search(queryArg(req, "keywords"), error))
        return sendJson(req, 502, jsonError(error));
    std::string out = "{\"ok\":true,\"tracks\":[";
    out.reserve(2400);
    const auto& results = Netease.results();
    for (size_t i = 0; i < results.size(); ++i) {
        if (i) out += ',';
        const OnlineTrack& track = results[i];
        out += "{\"id\":" + jsonQuote(u64str(track.id));
        out += ",\"title\":" + jsonQuote(track.title);
        out += ",\"artist\":" + jsonQuote(track.artist);
        out += ",\"album\":" + jsonQuote(track.album);
        out += ",\"duration\":" + std::to_string(track.duration) + "}";
    }
    out += "]}";
    return sendJson(req, 200, out);
}

esp_err_t handleNeteasePlay(httpd_req_t* req, WebControl* web) {
    const FormArgs form = FormArgs::parse(readBody(req, MAX_FORM_BODY));
    const std::string value = arg(req, form, "id");
    char* end = nullptr;
    const uint64_t id = strtoull(value.c_str(), &end, 10);
    OnlineTrack track;
    if (value.empty() || (end && *end) || !Netease.resultById(id, track))
        return sendJson(req, 400, jsonError("歌曲不在当前搜索结果中，请重新搜索"));
    OnlinePlayback playback;
    std::string error;
    if (!Netease.resolve(track, playback, error)) return sendJson(req, 502, jsonError(error));
    Audio.playOnline(track, playback);
    App.playerOpen = true;
    return handleState(req, web);
}

esp_err_t handleNeteaseLyric(httpd_req_t* req, WebControl* web) {
    const std::string value = queryArg(req, "id");
    char* end = nullptr;
    const uint64_t id = strtoull(value.c_str(), &end, 10);
    if (value.empty() || (end && *end)) return sendJson(req, 400, jsonError("歌曲 ID 无效"));
    return sendText(req, 200, "text/plain; charset=utf-8", Netease.cachedLyric(id));
}

esp_err_t handleReboot(httpd_req_t* req, WebControl* web) {
    sendJson(req, 200, "{\"ok\":true,\"reboot\":true}");
    web->scheduleReboot(800);
    return ESP_OK;
}

esp_err_t handleRescan(httpd_req_t* req, WebControl* web) {
    if (!Audio.storageBegin()) {
        Audio.storageEnd();
        return sendJson(req, 503, "{\"ok\":false,\"error\":\"音频任务未停止，请稍后重试\"}");
    }
    const bool ok = Library.ready() ? Library.rescan() : Library.begin();
    web->cacheSDCapacity();
    App.selected = 0;
    Audio.storageEnd();
    return sendJson(req, ok ? 200 : 500,
                    ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"未检测到 SD 卡\"}");
}

esp_err_t handleNotFound(httpd_req_t* req, WebControl* web) {
    return sendJson(req, 404, "{\"ok\":false,\"error\":\"Not found\"}");
}

// Trampoline: authenticate, then dispatch. Upload routes authenticate inside
// their handlers because they must inspect the multipart stream first.
#define AUTH_ROUTE(handler)                                                   \
    static esp_err_t tramp_##handler(httpd_req_t* req) {                      \
        auto* web = static_cast<WebControl*>(req->user_ctx);                  \
        if (!authorize(web, req)) return ESP_OK;                              \
        return handler(req, web);                                             \
    }

AUTH_ROUTE(handleRoot)
AUTH_ROUTE(handleState)
AUTH_ROUTE(handleTracks)
AUTH_ROUTE(handleControl)
AUTH_ROUTE(handleSettings)
AUTH_ROUTE(handleNetworkSettings)
AUTH_ROUTE(handleWifiScan)
AUTH_ROUTE(handleWifiConnect)
AUTH_ROUTE(handleNeteaseConfig)
AUTH_ROUTE(handleNeteaseStatus)
AUTH_ROUTE(handleNeteaseQrStart)
AUTH_ROUTE(handleNeteaseQrCheck)
AUTH_ROUTE(handleNeteaseLogin)
AUTH_ROUTE(handleNeteaseLogout)
AUTH_ROUTE(handleNeteaseSearch)
AUTH_ROUTE(handleNeteasePlay)
AUTH_ROUTE(handleNeteaseLyric)
AUTH_ROUTE(handleReboot)
AUTH_ROUTE(handleRescan)
AUTH_ROUTE(handleNotFound)
AUTH_ROUTE(handleFileList)
AUTH_ROUTE(handleFileRead)
AUTH_ROUTE(handleFileWrite)
AUTH_ROUTE(handleFileDownload)
AUTH_ROUTE(handleMkdir)
AUTH_ROUTE(handleRename)
AUTH_ROUTE(handleDelete)
AUTH_ROUTE(handleSDUpload)
AUTH_ROUTE(handleOTAUpload)

}  // namespace

void WebControl::begin() {
    if (!Settings.webEnabled) {
        wifi_stop();
        return;
    }
    _activeWebPassword = Settings.webPassword;
    Netease.begin();
    wifi_start();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 32;
    config.stack_size = 10240;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    if (httpd_start(&_server, &config) != ESP_OK) {
        ESP_LOGE(kTag, "httpd start failed");
        return;
    }
    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, tramp_handleRoot, this},
        {"/api/state", HTTP_GET, tramp_handleState, this},
        {"/api/tracks", HTTP_GET, tramp_handleTracks, this},
        {"/api/control", HTTP_POST, tramp_handleControl, this},
        {"/api/settings", HTTP_POST, tramp_handleSettings, this},
        {"/api/network", HTTP_POST, tramp_handleNetworkSettings, this},
        {"/api/wifi/scan", HTTP_GET, tramp_handleWifiScan, this},
        {"/api/wifi/connect", HTTP_POST, tramp_handleWifiConnect, this},
        {"/api/netease/config", HTTP_POST, tramp_handleNeteaseConfig, this},
        {"/api/netease/status", HTTP_POST, tramp_handleNeteaseStatus, this},
        {"/api/netease/qr/start", HTTP_POST, tramp_handleNeteaseQrStart, this},
        {"/api/netease/qr/check", HTTP_POST, tramp_handleNeteaseQrCheck, this},
        {"/api/netease/login", HTTP_POST, tramp_handleNeteaseLogin, this},
        {"/api/netease/logout", HTTP_POST, tramp_handleNeteaseLogout, this},
        {"/api/netease/search", HTTP_GET, tramp_handleNeteaseSearch, this},
        {"/api/netease/play", HTTP_POST, tramp_handleNeteasePlay, this},
        {"/api/netease/lyric", HTTP_GET, tramp_handleNeteaseLyric, this},
        {"/api/rescan", HTTP_POST, tramp_handleRescan, this},
        {"/api/reboot", HTTP_POST, tramp_handleReboot, this},
        {"/api/files", HTTP_GET, tramp_handleFileList, this},
        {"/api/sd/read", HTTP_GET, tramp_handleFileRead, this},
        {"/api/sd/write", HTTP_POST, tramp_handleFileWrite, this},
        {"/api/sd/download", HTTP_GET, tramp_handleFileDownload, this},
        {"/api/sd/mkdir", HTTP_POST, tramp_handleMkdir, this},
        {"/api/sd/rename", HTTP_POST, tramp_handleRename, this},
        {"/api/sd/delete", HTTP_POST, tramp_handleDelete, this},
        {"/api/sd/upload", HTTP_POST, tramp_handleSDUpload, this},
        {"/api/ota", HTTP_POST, tramp_handleOTAUpload, this},
        {"/*", HTTP_GET, tramp_handleNotFound, this},
    };
    for (const auto& route : routes) httpd_register_uri_handler(_server, &route);
    _running = true;
    if (Audio.storageBegin()) cacheSDCapacity();
    Audio.storageEnd();
    printf("WebUI: http://%s/ (AP http://%s/)\n", wifi_sta_ip().c_str(), wifi_ap_ip().c_str());
}

}  // namespace lz
