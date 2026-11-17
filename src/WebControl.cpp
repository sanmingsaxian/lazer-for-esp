#include "WebControl.h"

#include <algorithm>
#include <vector>
#include "App.h"
#include "WebUI.h"

WebControl Web;

namespace {
constexpr size_t MAX_TEXT_FILE = 96 * 1024;
constexpr size_t MAX_LIST_ENTRIES = 300;

String quoted(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    out = "\"";
    for (size_t i = 0; i < value.length(); ++i) {
        const uint8_t c = (uint8_t)value[i];
        if (c == '\"' || c == '\\') {
            out += '\\';
            out += (char)c;
        } else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            char escape[7];
            snprintf(escape, sizeof(escape), "\\u%04x", c);
            out += escape;
        } else out += (char)c;
    }
    out += '\"';
    return out;
}

bool argBool(const String& value) {
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

String baseName(const String& path) {
    const int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.substring(slash + 1) : path;
}

String parentPath(const String& path) {
    if (path == "/") return "/";
    const int slash = path.lastIndexOf('/');
    return slash <= 0 ? "/" : path.substring(0, slash);
}

String joinPath(const String& parent, const String& name) {
    return parent == "/" ? "/" + name : parent + "/" + name;
}

bool normalizePath(const String& input, String& output) {
    if (!input.length() || input.length() > 180 || input[0] != '/') return false;
    output = "/";
    int cursor = 1;
    while (cursor <= (int)input.length()) {
        int slash = input.indexOf('/', cursor);
        if (slash < 0) slash = input.length();
        const String part = input.substring(cursor, slash);
        if (part.length() && part != ".") {
            if (part == "..") return false;
            for (size_t i = 0; i < part.length(); ++i) {
                const uint8_t c = (uint8_t)part[i];
                if (c < 0x20 || c == '\\') return false;
            }
            if (output != "/") output += '/';
            output += part;
            if (output.length() > 180) return false;
        }
        if (slash == (int)input.length()) break;
        cursor = slash + 1;
    }
    return true;
}

bool validChildName(const String& name) {
    if (!name.length() || name.length() > 80 || name == "." || name == "..") return false;
    const char* forbidden = "\\/:*?\"<>|";
    for (size_t i = 0; i < name.length(); ++i) {
        const uint8_t c = (uint8_t)name[i];
        if (c < 0x20 || strchr(forbidden, c)) return false;
    }
    return true;
}

bool protectedSystemPath(const String& path) {
    String lower = path;
    lower.toLowerCase();
    return lower == "/system volume information" ||
           lower.startsWith("/system volume information/");
}

String lowerCopy(String value) {
    value.toLowerCase();
    return value;
}

String unsigned64(uint64_t value) {
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
    return String(buffer);
}

String jsonError(const String& message) {
    return String("{\"ok\":false,\"error\":") + quoted(message) + "}";
}

bool textFilePath(const String& path) {
    const String lower = lowerCopy(path);
    const char* extensions[] = {".lrc", ".txt", ".md", ".json", ".csv", ".log", ".m3u", ".m3u8"};
    for (const char* extension : extensions) {
        if (lower.endsWith(extension)) return true;
    }
    return false;
}

String contentTypeFor(const String& path) {
    const String lower = lowerCopy(path);
    if (lower.endsWith(".mp3")) return "audio/mpeg";
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
    if (lower.endsWith(".png")) return "image/png";
    if (lower.endsWith(".gif")) return "image/gif";
    if (lower.endsWith(".json")) return "application/json";
    if (lower.endsWith(".csv")) return "text/csv; charset=utf-8";
    if (textFilePath(lower)) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

String urlEncode(const String& value) {
    const char* hex = "0123456789ABCDEF";
    String out;
    for (size_t i = 0; i < value.length(); ++i) {
        const uint8_t c = (uint8_t)value[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

struct StorageGuard {
    bool ok;
    StorageGuard() : ok(Audio.storageBegin()) {
        if (!ok) Audio.storageEnd();
    }
    ~StorageGuard() { Audio.storageEnd(); }
};

const char* audioStateName(AudioState state) {
    switch (state) {
        case AudioState::Loading: return "loading";
        case AudioState::Playing: return "playing";
        case AudioState::Paused: return "paused";
        case AudioState::Error: return "error";
        default: return "empty";
    }
}

bool removeRecursive(const String& path, uint8_t depth) {
    if (depth > 6) return false;
    File file = SD.open(path, FILE_READ);
    if (!file) return false;
    if (!file.isDirectory()) {
        file.close();
        return SD.remove(path);
    }
    File child = file.openNextFile();
    while (child) {
        const String childPath = child.path();
        const bool directory = child.isDirectory();
        child.close();
        bool ok = directory ? removeRecursive(childPath, depth + 1) : SD.remove(childPath);
        if (!ok) {
            file.close();
            return false;
        }
        child = file.openNextFile();
    }
    file.close();
    return SD.rmdir(path);
}
}  // namespace

void WebControl::begin() {
    if (!Settings.webEnabled) {
        WiFi.mode(WIFI_OFF);
        return;
    }
    _activeWebPassword = Settings.webPassword;
    applyNetwork();
    installRoutes();
    _server.begin(80);
    _running = true;
    if (Audio.storageBegin()) cacheSDCapacityLocked();
    Audio.storageEnd();
    Serial.printf("WebUI: http://%s/ (AP http://%s/)\n",
                  WiFi.localIP().toString().c_str(), WiFi.softAPIP().toString().c_str());
}

void WebControl::applyNetwork() {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setHostname("lazer-cardputer");
    // Reduce radio current peaks while the screen and NS4168 amplifier are active.
    // Keep WPA2 and the local fallback AP; only range and client capacity are reduced.
    const bool txPower = WiFi.setTxPower(WIFI_POWER_8_5dBm);
    const bool ap = WiFi.softAP(Settings.apSsid.c_str(), Settings.apPassword.c_str(), 1, 0, 2);
    Serial.printf("WebUI radio: txPower=8.5dBm (%s), maxClients=2\n", txPower ? "ok" : "failed");
    Serial.printf("WebUI AP %s: %s, IP=%s\n", Settings.apSsid.c_str(), ap ? "started" : "failed",
                  WiFi.softAPIP().toString().c_str());
    if (Settings.staSsid.length()) {
        WiFi.begin(Settings.staSsid.c_str(), Settings.staPassword.c_str());
        Serial.printf("WebUI STA connecting to %s\n", Settings.staSsid.c_str());
    }
}

void WebControl::update() {
    if (_rebootAt && millis() >= _rebootAt) {
        _rebootAt = 0;
        delay(80);
        ESP.restart();
    }
    if (!_running) return;
    _server.handleClient();
    if (Settings.staSsid.length() && WiFi.status() != WL_CONNECTED &&
        millis() - _lastReconnect > 20000) {
        _lastReconnect = millis();
        WiFi.reconnect();
    }
}

String WebControl::address() const {
    if (!_running) return "关闭";
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        return WiFi.localIP().toString();
    }
    return WiFi.softAPIP().toString();
}

String WebControl::connectionLabel() const {
    if (!_running) return "WebUI 关闭";
    return String(WiFi.status() == WL_CONNECTED ? "STA " : "AP ") + address();
}

bool WebControl::authorize() {
    if (_server.authenticate("admin", _activeWebPassword.c_str())) return true;
    _server.requestAuthentication(BASIC_AUTH, "Lazer Cardputer", "请输入 WebUI 管理密码");
    return false;
}

void WebControl::sendJson(int status, const String& json) {
    _server.sendHeader("Cache-Control", "no-store");
    _server.send(status, "application/json; charset=utf-8", json);
}

void WebControl::handleRoot() {
    if (!authorize()) return;
    _server.sendHeader("Cache-Control", "no-store");
    _server.sendHeader("Content-Security-Policy",
                       "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; img-src 'self' data:;");
    _server.send_P(200, "text/html; charset=utf-8", WEB_UI);
}

void WebControl::installRoutes() {
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/api/state", HTTP_GET, [this]() { if (authorize()) handleState(); });
    _server.on("/api/tracks", HTTP_GET, [this]() { if (authorize()) handleTracks(); });
    _server.on("/api/control", HTTP_POST, [this]() { if (authorize()) handleControl(); });
    _server.on("/api/settings", HTTP_POST, [this]() { if (authorize()) handleSettings(); });
    _server.on("/api/network", HTTP_POST, [this]() { if (authorize()) handleNetworkSettings(); });
    _server.on("/api/rescan", HTTP_POST, [this]() { if (authorize()) handleRescan(); });
    _server.on("/api/reboot", HTTP_POST, [this]() { if (authorize()) handleReboot(); });
    _server.on("/api/files", HTTP_GET, [this]() { if (authorize()) handleFileList(); });
    _server.on("/api/sd/read", HTTP_GET, [this]() { if (authorize()) handleFileRead(); });
    _server.on("/api/sd/write", HTTP_POST, [this]() { if (authorize()) handleFileWrite(); });
    _server.on("/api/sd/download", HTTP_GET, [this]() { if (authorize()) handleFileDownload(); });
    _server.on("/api/sd/mkdir", HTTP_POST, [this]() { if (authorize()) handleMkdir(); });
    _server.on("/api/sd/rename", HTTP_POST, [this]() { if (authorize()) handleRename(); });
    _server.on("/api/sd/delete", HTTP_POST, [this]() { if (authorize()) handleDelete(); });
    _server.on("/api/sd/upload", HTTP_POST,
               [this]() { if (authorize()) handleSDUploadComplete(); },
               [this]() { handleSDUpload(); });
    _server.on("/api/ota", HTTP_POST,
               [this]() { if (authorize()) handleOTAUploadComplete(); },
               [this]() { handleOTAUpload(); });
    _server.onNotFound([this]() {
        if (authorize()) sendJson(404, "{\"ok\":false,\"error\":\"Not found\"}");
    });
}

void WebControl::handleState() {
    const Track* track = Audio.currentTrack() >= 0 ? Library.track(Audio.currentTrack()) : nullptr;
    String out;
    out.reserve(2200);
    out = "{";
    out += "\"ok\":true,\"version\":" + quoted(LAZER_VERSION);
    out += ",\"device\":\"Cardputer v1.1 / Stamp-S3A\"";
    out += ",\"playing\":" + String(Audio.playing() ? "true" : "false");
    out += ",\"state\":" + quoted(audioStateName(Audio.state()));
    out += ",\"trackIndex\":" + String(Audio.currentTrack());
    out += ",\"title\":" + quoted(track ? track->title : "未选择曲目");
    out += ",\"artist\":" + quoted(track ? track->artist : "Lazer Cardputer");
    out += ",\"album\":" + quoted(track ? track->album : "本地音乐");
    out += ",\"path\":" + quoted(track ? track->path : "");
    out += ",\"position\":" + String(Audio.position());
    out += ",\"duration\":" + String(Audio.duration());
    out += ",\"volume\":" + String(Settings.volume);
    out += ",\"brightness\":" + String(Settings.brightness);
    out += ",\"dark\":" + String(Settings.dark ? "true" : "false");
    out += ",\"lyrics\":" + String(Settings.showLyrics ? "true" : "false");
    out += ",\"reduceMotion\":" + String(Settings.reduceMotion ? "true" : "false");
    out += ",\"mode\":" + String((uint8_t)Settings.playMode);
    out += ",\"error\":" + quoted(Audio.error());
    out += ",\"sdReady\":" + String(Library.ready() ? "true" : "false");
    out += ",\"trackCount\":" + String(Library.count());
    out += ",\"sdTotal\":" + unsigned64(_sdTotal);
    out += ",\"sdUsed\":" + unsigned64(_sdUsed);
    out += ",\"apSsid\":" + quoted(Settings.apSsid);
    out += ",\"staSsid\":" + quoted(Settings.staSsid);
    out += ",\"apIp\":" + quoted(WiFi.softAPIP().toString());
    out += ",\"staIp\":" + quoted(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "");
    out += ",\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
    out += ",\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
    out += ",\"freeHeap\":" + String(ESP.getFreeHeap());
    out += ",\"minFreeHeap\":" + String(ESP.getMinFreeHeap());
    out += ",\"maxAllocHeap\":" + String(ESP.getMaxAllocHeap());
    out += ",\"audioStackHwm\":" + String(Audio.stackHighWater());
    out += ",\"bootReset\":" + quoted(App.bootReset);
    out += ",\"bootResetCode\":" + String(App.bootResetCode);
    out += ",\"uptime\":" + String(millis() / 1000);
    out += "}";
    sendJson(200, out);
}

void WebControl::sendState() { handleState(); }

void WebControl::handleTracks() {
    String out = "{\"ok\":true,\"tracks\":[";
    out.reserve(8192);
    for (size_t i = 0; i < Library.count(); ++i) {
        const Track* track = Library.track(i);
        if (i) out += ',';
        out += "{\"index\":" + String(i);
        out += ",\"title\":" + quoted(track->title);
        out += ",\"artist\":" + quoted(track->artist);
        out += ",\"album\":" + quoted(track->album);
        out += ",\"path\":" + quoted(track->path);
        out += ",\"duration\":" + String(track->duration);
        out += ",\"size\":" + String(track->fileSize);
        out += ",\"favorite\":" + String(Settings.isFavorite(track->seed) ? "true" : "false");
        out += "}";
    }
    out += "]}";
    sendJson(200, out);
}

void WebControl::handleControl() {
    const String action = _server.arg("action");
    if (action == "toggle") Audio.toggle();
    else if (action == "next") Audio.next();
    else if (action == "previous") Audio.previous();
    else if (action == "stop") Audio.eject();
    else if (action == "play") {
        const int index = _server.arg("index").toInt();
        if (!Library.track(index)) {
            sendJson(400, "{\"ok\":false,\"error\":\"曲目不存在\"}");
            return;
        }
        Audio.play(index);
        App.playerOpen = true;
    } else if (action == "seek") {
        if (Audio.currentTrack() < 0) {
            sendJson(400, "{\"ok\":false,\"error\":\"尚未选择曲目\"}");
            return;
        }
        uint32_t target = (uint32_t)constrain(_server.arg("seconds").toInt(), 0, 86400);
        const uint32_t duration = Audio.duration();
        if (duration && target >= duration) target = duration > 1 ? duration - 1 : 0;
        Audio.play(Audio.currentTrack(), target);
    } else if (action == "mode") {
        const int mode = _server.arg("value").toInt();
        if (mode < 0 || mode > 2) {
            sendJson(400, "{\"ok\":false,\"error\":\"播放模式无效\"}");
            return;
        }
        Settings.playMode = (PlayMode)mode;
        Audio.setPlayMode(Settings.playMode);
        Settings.save();
    } else if (action == "favorite") {
        int index = Audio.currentTrack();
        if (_server.hasArg("index")) index = _server.arg("index").toInt();
        const Track* track = Library.track(index);
        if (!track) {
            sendJson(400, "{\"ok\":false,\"error\":\"曲目不存在\"}");
            return;
        }
        Settings.toggleFavorite(track->seed);
    } else {
        sendJson(400, "{\"ok\":false,\"error\":\"未知控制命令\"}");
        return;
    }
    sendState();
}

void WebControl::handleSettings() {
    if (_server.hasArg("volume")) {
        Settings.volume = constrain(_server.arg("volume").toInt(), 0, 20);
        Audio.setVolume(Settings.volume);
    }
    if (_server.hasArg("brightness")) {
        Settings.brightness = constrain(_server.arg("brightness").toInt(), 40, 255);
        M5Cardputer.Display.setBrightness(Settings.brightness);
    }
    if (_server.hasArg("dark")) Settings.dark = argBool(_server.arg("dark"));
    if (_server.hasArg("lyrics")) {
        Settings.showLyrics = argBool(_server.arg("lyrics"));
        App.lyricsMode = Settings.showLyrics;
    }
    if (_server.hasArg("reduceMotion")) Settings.reduceMotion = argBool(_server.arg("reduceMotion"));
    if (_server.hasArg("mode")) {
        const int mode = constrain(_server.arg("mode").toInt(), 0, 2);
        Settings.playMode = (PlayMode)mode;
        Audio.setPlayMode(Settings.playMode);
    }
    Settings.save();
    sendState();
}

void WebControl::handleNetworkSettings() {
    String apSsid = _server.arg("apSsid");
    apSsid.trim();
    const String apPassword = _server.arg("apPassword");
    const String webPassword = _server.arg("webPassword");
    const String staSsid = _server.arg("staSsid");
    const String staPassword = _server.arg("staPassword");
    if (!apSsid.length() || apSsid.length() > 32 || staSsid.length() > 32 ||
        staPassword.length() > 63 || apPassword.length() > 63 || webPassword.length() > 63 ||
        (apPassword.length() && apPassword.length() < 8) ||
        (webPassword.length() && webPassword.length() < 8)) {
        sendJson(400, "{\"ok\":false,\"error\":\"名称或密码长度无效；新密码至少 8 位\"}");
        return;
    }
    const bool sameStation = staSsid == Settings.staSsid;
    Settings.staSsid = staSsid;
    Settings.staSsid.trim();
    if (!Settings.staSsid.length()) Settings.staPassword = "";
    else if (staPassword.length() || !sameStation) Settings.staPassword = staPassword;
    Settings.apSsid = apSsid;
    if (apPassword.length()) Settings.apPassword = apPassword;
    if (webPassword.length()) Settings.webPassword = webPassword;
    Settings.webEnabled = true;
    Settings.save();
    // The current authenticated session keeps using the old password until
    // reboot, so the reboot action cannot be locked out by its own save.
    sendJson(200, "{\"ok\":true,\"rebootRequired\":true}");
}

void WebControl::handleReboot() {
    sendJson(200, "{\"ok\":true,\"reboot\":true}");
    _rebootAt = millis() + 800;
}

void WebControl::handleRescan() {
    StorageGuard storage;
    if (!storage.ok) {
        sendJson(503, "{\"ok\":false,\"error\":\"音频任务未停止，请稍后重试\"}");
        return;
    }
    const bool ok = Library.ready() ? Library.rescan() : Library.begin();
    cacheSDCapacityLocked();
    App.selected = 0;
    sendJson(ok ? 200 : 500, ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"未检测到 SD 卡\"}");
}


void WebControl::cacheSDCapacityLocked() {
    if (Library.ready()) {
        _sdTotal = SD.totalBytes();
        _sdUsed = SD.usedBytes();
    } else {
        _sdTotal = 0;
        _sdUsed = 0;
    }
}

void WebControl::refreshLibraryLocked() {
    if (Library.ready()) Library.rescan();
    else Library.begin();
    cacheSDCapacityLocked();
    App.selected = 0;
}

void WebControl::handleFileList() {
    String path;
    if (!normalizePath(_server.hasArg("path") ? _server.arg("path") : "/", path)) {
        sendJson(400, jsonError("目录路径无效"));
        return;
    }
    StorageGuard storage;
    if (!storage.ok) {
        sendJson(503, jsonError("音频任务未停止，请稍后重试"));
        return;
    }
    if (!Library.ready()) Library.begin();
    File directory = SD.open(path, FILE_READ);
    if (!directory || !directory.isDirectory()) {
        if (directory) directory.close();
        sendJson(404, jsonError("目录不存在或 SD 卡不可用"));
        return;
    }

    struct Entry {
        String name;
        String path;
        bool directory;
        uint32_t size;
    };
    std::vector<Entry> entries;
    entries.reserve(48);
    bool truncated = false;
    File child = directory.openNextFile();
    while (child) {
        if (entries.size() >= MAX_LIST_ENTRIES) {
            truncated = true;
            child.close();
            break;
        }
        String name = baseName(child.path());
        String lower = lowerCopy(name);
        if (!name.startsWith(".") && !lower.endsWith(".lazer-upload") &&
            !lower.endsWith(".lazer-save") && name != "System Volume Information") {
            entries.push_back({name, child.path(), child.isDirectory(), child.isDirectory() ? 0 : child.size()});
        }
        child.close();
        child = directory.openNextFile();
    }
    directory.close();
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.directory != b.directory) return a.directory;
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    cacheSDCapacityLocked();

    String out;
    out.reserve(4096 + entries.size() * 120);
    out = "{\"ok\":true,\"path\":" + quoted(path);
    out += ",\"parent\":" + quoted(parentPath(path));
    out += ",\"truncated\":" + String(truncated ? "true" : "false");
    out += ",\"entries\":[";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i) out += ',';
        out += "{\"name\":" + quoted(entries[i].name);
        out += ",\"path\":" + quoted(entries[i].path);
        out += ",\"directory\":" + String(entries[i].directory ? "true" : "false");
        out += ",\"size\":" + String(entries[i].size) + "}";
    }
    out += "]}";
    sendJson(200, out);
}

void WebControl::handleFileRead() {
    String path;
    if (!normalizePath(_server.arg("path"), path) || protectedSystemPath(path) || !textFilePath(path)) {
        sendJson(400, jsonError("仅支持读取 LRC、TXT、MD、JSON、CSV、LOG 和播放列表文本"));
        return;
    }
    StorageGuard storage;
    if (!storage.ok) {
        sendJson(503, jsonError("音频任务未停止，请稍后重试"));
        return;
    }
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        sendJson(404, jsonError("文件不存在"));
        return;
    }
    if (file.size() > MAX_TEXT_FILE) {
        file.close();
        sendJson(413, jsonError("文本超过 96 KiB，请下载后编辑"));
        return;
    }
    String content;
    content.reserve(file.size() + 1);
    uint8_t buffer[1024];
    while (file.available()) {
        const size_t count = file.read(buffer, sizeof(buffer));
        if (!count) break;
        content.concat((const char*)buffer, count);
    }
    file.close();
    _server.sendHeader("Cache-Control", "no-store");
    _server.send(200, "text/plain; charset=utf-8", content);
}

void WebControl::handleFileWrite() {
    String path;
    if (!normalizePath(_server.arg("path"), path) || path == "/" || protectedSystemPath(path) ||
        !textFilePath(path)) {
        sendJson(400, jsonError("文本保存路径或扩展名无效"));
        return;
    }
    const String content = _server.hasArg("plain") ? _server.arg("plain") : _server.arg("content");
    if (content.length() > MAX_TEXT_FILE) {
        sendJson(413, jsonError("文本超过 96 KiB"));
        return;
    }
    const String parent = parentPath(path);
    const String temp = path + ".lazer-save";
    if (temp.length() > 180) {
        sendJson(400, jsonError("文件路径过长"));
        return;
    }
    StorageGuard storage;
    if (!storage.ok) {
        sendJson(503, jsonError("音频任务未停止，请稍后重试"));
        return;
    }
    File directory = SD.open(parent, FILE_READ);
    if (!directory || !directory.isDirectory()) {
        if (directory) directory.close();
        sendJson(404, jsonError("目标目录不存在"));
        return;
    }
    directory.close();
    File old = SD.open(path, FILE_READ);
    if (old && old.isDirectory()) {
        old.close();
        sendJson(409, jsonError("不能覆盖目录"));
        return;
    }
    if (old) old.close();
    SD.remove(temp);
    File file = SD.open(temp, FILE_WRITE);
    if (!file) {
        sendJson(500, jsonError("无法创建临时文件"));
        return;
    }
    const size_t written = file.write((const uint8_t*)content.c_str(), content.length());
    file.close();
    if (written != content.length()) {
        SD.remove(temp);
        sendJson(500, jsonError("写入不完整，SD 卡可能已满"));
        return;
    }
    if (SD.exists(path) && !SD.remove(path)) {
        SD.remove(temp);
        sendJson(500, jsonError("无法替换原文件"));
        return;
    }
    if (!SD.rename(temp, path)) {
        SD.remove(temp);
        sendJson(500, jsonError("保存失败"));
        return;
    }
    cacheSDCapacityLocked();
    sendJson(200, "{\"ok\":true,\"bytes\":" + String(content.length()) + "}");
}

void WebControl::handleFileDownload() {
    String path;
    if (!normalizePath(_server.arg("path"), path) || path == "/" || protectedSystemPath(path)) {
        sendJson(400, jsonError("下载路径无效"));
        return;
    }
    StorageGuard storage;
    if (!storage.ok) {
        sendJson(503, jsonError("音频任务未停止，请稍后重试"));
        return;
    }
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        sendJson(404, jsonError("文件不存在或目录不能直接下载"));
        return;
    }
    const String name = baseName(path);
    _server.sendHeader("Cache-Control", "no-store");
    _server.sendHeader("Content-Disposition",
                       "attachment; filename=\"download\"; filename*=UTF-8''" + urlEncode(name));
    _server.streamFile(file, contentTypeFor(path));
    file.close();
}

void WebControl::handleMkdir() {
    String parent;
    const String name = _server.arg("name");
    if (!normalizePath(_server.arg("path"), parent) || !validChildName(name)) {
        sendJson(400, jsonError("目录名称无效"));
        return;
    }
    const String target = joinPath(parent, name);
    if (target.length() > 180 || protectedSystemPath(target)) {
        sendJson(400, jsonError("不允许创建该目录"));
        return;
    }
    StorageGuard storage;
    if (!storage.ok) {
        sendJson(503, jsonError("音频任务未停止，请稍后重试"));
        return;
    }
    if (SD.exists(target)) {
        sendJson(409, jsonError("同名文件或目录已存在"));
        return;
    }
    if (!SD.mkdir(target)) {
        sendJson(500, jsonError("创建目录失败"));
        return;
    }
    refreshLibraryLocked();
    sendJson(200, "{\"ok\":true,\"path\":" + quoted(target) + "}");
}

void WebControl::handleRename() {
    String source;
    const String name = _server.arg("name");
    if (!normalizePath(_server.arg("path"), source) || source == "/" ||
        protectedSystemPath(source) || !validChildName(name)) {
        sendJson(400, jsonError("原路径或新名称无效"));
        return;
    }
    const String target = joinPath(parentPath(source), name);
    if (target.length() > 180 || protectedSystemPath(target)) {
        sendJson(400, jsonError("目标名称无效"));
        return;
    }
    StorageGuard storage;
    if (!storage.ok) {
        sendJson(503, jsonError("音频任务未停止，请稍后重试"));
        return;
    }
    if (!SD.exists(source)) {
        sendJson(404, jsonError("原文件不存在"));
        return;
    }
    if (SD.exists(target)) {
        sendJson(409, jsonError("目标名称已存在"));
        return;
    }
    if (!SD.rename(source, target)) {
        sendJson(500, jsonError("重命名失败"));
        return;
    }
    refreshLibraryLocked();
    sendJson(200, "{\"ok\":true,\"path\":" + quoted(target) + "}");
}

void WebControl::handleDelete() {
    String path;
    if (!normalizePath(_server.arg("path"), path) || path == "/" || protectedSystemPath(path)) {
        sendJson(400, jsonError("不能删除该路径"));
        return;
    }
    StorageGuard storage;
    if (!storage.ok) {
        sendJson(503, jsonError("音频任务未停止，请稍后重试"));
        return;
    }
    File file = SD.open(path, FILE_READ);
    if (!file) {
        sendJson(404, jsonError("文件或目录不存在"));
        return;
    }
    const bool directory = file.isDirectory();
    file.close();
    bool ok = false;
    if (directory && argBool(_server.arg("recursive"))) ok = removeRecursive(path, 0);
    else ok = directory ? SD.rmdir(path) : SD.remove(path);
    if (!ok) {
        sendJson(500, jsonError(directory ? "目录删除失败；非空目录需确认递归删除" : "文件删除失败"));
        return;
    }
    refreshLibraryLocked();
    sendJson(200, "{\"ok\":true}");
}

void WebControl::handleSDUpload() {
    HTTPUpload& upload = _server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        if (_sdUploadActive) {
            _sdUploadFile.close();
            SD.remove(_sdUploadTemp);
            _sdUploadActive = false;
            Audio.storageEnd();
        }
        _sdUploadError = "";
        _sdUploadFinal = "";
        _sdUploadTemp = "";
        _sdUploadBytes = 0;
        _sdUploadOverwrite = argBool(_server.arg("overwrite"));
        if (!_server.authenticate("admin", _activeWebPassword.c_str())) {
            _sdUploadError = "未认证";
            return;
        }
        String parent;
        String name = upload.filename;
        const int windowsSlash = name.lastIndexOf('\\');
        if (windowsSlash >= 0) name = name.substring(windowsSlash + 1);
        name = baseName(name);
        if (!normalizePath(_server.arg("path"), parent) || !validChildName(name)) {
            _sdUploadError = "上传目录或文件名无效";
            return;
        }
        _sdUploadFinal = joinPath(parent, name);
        _sdUploadTemp = _sdUploadFinal + ".lazer-upload";
        if (_sdUploadTemp.length() > 180 || protectedSystemPath(_sdUploadFinal)) {
            _sdUploadError = "不允许写入该路径";
            return;
        }
        if (!Audio.storageBegin()) {
            Audio.storageEnd();
            _sdUploadError = "音频任务未停止，请稍后重试";
            return;
        }
        if (!Library.ready()) Library.begin();
        File directory = SD.open(parent, FILE_READ);
        if (!directory || !directory.isDirectory()) {
            if (directory) directory.close();
            Audio.storageEnd();
            _sdUploadError = "目标目录不存在或 SD 卡不可用";
            return;
        }
        directory.close();
        File existing = SD.open(_sdUploadFinal, FILE_READ);
        if (existing && (existing.isDirectory() || !_sdUploadOverwrite)) {
            const bool isDirectory = existing.isDirectory();
            existing.close();
            Audio.storageEnd();
            _sdUploadError = isDirectory ? "不能覆盖目录" : "文件已存在；勾选覆盖后重试";
            return;
        }
        if (existing) existing.close();
        SD.remove(_sdUploadTemp);
        _sdUploadFile = SD.open(_sdUploadTemp, FILE_WRITE);
        if (!_sdUploadFile) {
            Audio.storageEnd();
            _sdUploadError = "无法创建上传临时文件";
            return;
        }
        _sdUploadActive = true;
    } else if (upload.status == UPLOAD_FILE_WRITE && _sdUploadActive) {
        const size_t written = _sdUploadFile.write(upload.buf, upload.currentSize);
        _sdUploadBytes += written;
        if (written != upload.currentSize) {
            _sdUploadFile.close();
            SD.remove(_sdUploadTemp);
            _sdUploadActive = false;
            Audio.storageEnd();
            _sdUploadError = "上传写入失败，SD 卡可能已满";
        }
    } else if (upload.status == UPLOAD_FILE_END && _sdUploadActive) {
        _sdUploadFile.close();
        _sdUploadActive = false;
        if (SD.exists(_sdUploadFinal) && !SD.remove(_sdUploadFinal)) {
            SD.remove(_sdUploadTemp);
            _sdUploadError = "无法替换原文件";
        } else if (!SD.rename(_sdUploadTemp, _sdUploadFinal)) {
            SD.remove(_sdUploadTemp);
            _sdUploadError = "上传完成但改名失败";
        } else {
            refreshLibraryLocked();
        }
        Audio.storageEnd();
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (_sdUploadActive) {
            _sdUploadFile.close();
            SD.remove(_sdUploadTemp);
            _sdUploadActive = false;
            Audio.storageEnd();
        }
        _sdUploadError = "上传已中止";
    }
}

void WebControl::handleSDUploadComplete() {
    if (_sdUploadError.length()) {
        const String message = _sdUploadError;
        _sdUploadError = "";
        sendJson(message == "未认证" ? 401 : 400, jsonError(message));
        return;
    }
    if (!_sdUploadFinal.length()) {
        sendJson(400, jsonError("没有收到上传文件"));
        return;
    }
    const String response = "{\"ok\":true,\"path\":" + quoted(_sdUploadFinal) +
                            ",\"bytes\":" + String(_sdUploadBytes) + "}";
    _sdUploadFinal = "";
    _sdUploadTemp = "";
    sendJson(200, response);
}

void WebControl::handleOTAUpload() {
    HTTPUpload& upload = _server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        if (_otaActive) {
            Update.abort();
            _otaActive = false;
            Audio.storageEnd();
        }
        _otaFinished = false;
        _otaError = "";
        _otaBytes = 0;
        if (!_server.authenticate("admin", _activeWebPassword.c_str())) {
            _otaError = "未认证";
            return;
        }
        String filename = lowerCopy(upload.filename);
        if (!filename.endsWith(".bin") || filename.indexOf("merged") >= 0 ||
            filename.indexOf("bootloader") >= 0 || filename.indexOf("partition") >= 0 ||
            filename.indexOf("otadata") >= 0) {
            _otaError = "请选择应用 app.bin，不能上传 merged、bootloader、分区表或 otadata";
            return;
        }
        if (!Audio.storageBegin()) {
            Audio.storageEnd();
            _otaError = "音频任务未停止，请稍后重试";
            return;
        }
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            _otaError = String("无法开始 OTA：") + Update.errorString();
            Audio.storageEnd();
            return;
        }
        _otaActive = true;
    } else if (upload.status == UPLOAD_FILE_WRITE && _otaActive) {
        if (!_otaBytes && upload.currentSize && upload.buf[0] != 0xE9) {
            _otaError = "文件不是有效的 ESP32 应用镜像";
        } else {
            const size_t written = Update.write(upload.buf, upload.currentSize);
            _otaBytes += written;
            if (written != upload.currentSize) _otaError = String("固件写入失败：") + Update.errorString();
        }
        if (_otaError.length()) {
            Update.abort();
            _otaActive = false;
            Audio.storageEnd();
        }
    } else if (upload.status == UPLOAD_FILE_END && _otaActive) {
        _otaActive = false;
        if (Update.end(true) && !Update.hasError()) {
            _otaFinished = true;
            // Keep the decoder stopped until the delayed reboot.
        } else {
            _otaError = String("固件校验失败：") + Update.errorString();
            Audio.storageEnd();
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (_otaActive) Update.abort();
        _otaActive = false;
        _otaFinished = false;
        _otaError = "OTA 上传已中止";
        Audio.storageEnd();
    }
}

void WebControl::handleOTAUploadComplete() {
    if (_otaError.length()) {
        const String message = _otaError;
        _otaError = "";
        sendJson(message == "未认证" ? 401 : 400, jsonError(message));
        return;
    }
    if (!_otaFinished || !_otaBytes) {
        sendJson(400, jsonError("OTA 尚未完成"));
        return;
    }
    _otaFinished = false;
    sendJson(200, "{\"ok\":true,\"bytes\":" + String(_otaBytes) + ",\"reboot\":true}");
    _rebootAt = millis() + 1500;
}
