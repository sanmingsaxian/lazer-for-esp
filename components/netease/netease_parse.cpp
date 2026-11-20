#include "netease_parse.h"
#include "strutil.h"

#include <cJSON.h>

namespace lz {

namespace {
std::string strOf(const cJSON* object, const char* key, const char* fallback) {
    if (!object) return fallback;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(item) && item->valuestring) return item->valuestring;
    return fallback;
}

uint64_t u64Of(const cJSON* object, const char* key) {
    if (!object) return 0;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(item)) return item->valuedouble > 0 ? (uint64_t)item->valuedouble : 0;
    return 0;
}

int codeOf(const cJSON* root, int fallback) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "code");
    return cJSON_IsNumber(item) ? (int)item->valuedouble : fallback;
}

const cJSON* objectOf(const cJSON* object, const char* key) {
    if (!object) return nullptr;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsObject(item) ? item : nullptr;
}
}  // namespace

std::string jsonMessageOf(const char* json, const std::string& fallback) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return fallback;
    std::string message = strOf(root, "message", "");
    if (message.empty()) message = strOf(root, "msg", "");
    cJSON_Delete(root);
    return message.empty() ? fallback : message;
}

bool parseSearchResponse(const char* json, std::vector<OnlineTrack>& out, std::string& error) {
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        error = "搜索响应不是有效 JSON";
        return false;
    }
    bool ok = false;
    if (codeOf(root, 0) != 200) {
        error = jsonMessageOf(json, "搜索失败");
    } else {
        std::vector<OnlineTrack> results;
        const cJSON* result = objectOf(root, "result");
        const cJSON* songs = result ? cJSON_GetObjectItemCaseSensitive(result, "songs") : nullptr;
        const cJSON* song = nullptr;
        cJSON_ArrayForEach(song, songs) {
            OnlineTrack track;
            track.id = u64Of(song, "id");
            if (!track.id) continue;
            track.title = strOf(song, "name", "未知歌曲");
            const cJSON* artists = cJSON_GetObjectItemCaseSensitive(song, "ar");
            if (!cJSON_IsArray(artists)) artists = cJSON_GetObjectItemCaseSensitive(song, "artists");
            int count = 0;
            const cJSON* artist = nullptr;
            cJSON_ArrayForEach(artist, artists) {
                if (count == 2) break;
                if (count++) track.artist += " / ";
                track.artist += strOf(artist, "name", "未知歌手");
            }
            if (track.artist.empty()) track.artist = "未知歌手";
            track.album = strOf(objectOf(song, "al"), "name", "");
            if (track.album.empty()) track.album = strOf(objectOf(song, "album"), "name", "");
            track.duration = (uint32_t)(u64Of(song, "dt") / 1000);
            if (!track.duration) track.duration = (uint32_t)(u64Of(song, "duration") / 1000);
            results.push_back(track);
        }
        out.swap(results);
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

bool parseResolveResponse(const char* json, OnlinePlayback& out, std::string& error) {
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        error = "歌曲地址响应不是有效 JSON";
        return false;
    }
    bool ok = false;
    if (codeOf(root, 0) != 200) {
        error = jsonMessageOf(json, "获取歌曲地址失败");
    } else {
        const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
        const cJSON* first = cJSON_IsArray(data) ? cJSON_GetArrayItem(data, 0) : nullptr;
        out.url = strOf(first, "url", "");
        out.format = toLower(strOf(first, "type", ""));
        out.bitrate = (uint32_t)u64Of(first, "br");
        out.size = u64Of(first, "size");
        if (out.url.empty()) {
            error = "此歌曲没有可播放地址，可能受版权或账号权限限制";
        } else if (!out.format.empty() && out.format != "mp3") {
            error = "返回格式为 " + out.format + "；当前设备仅支持在线 MP3";
        } else if (!startsWith(out.url, "http://") && !startsWith(out.url, "https://")) {
            error = "歌曲地址协议不受支持";
        } else {
            ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}

int parseQrCheckResponse(const char* json, std::string& cookie, std::string& nickname,
                         uint64_t& userId, std::string& error) {
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        error = "二维码状态响应无效";
        return -1;
    }
    const int code = codeOf(root, -1);
    int result = code;
    if (code == 803) {
        cookie = strOf(root, "cookie", "");
        nickname = strOf(root, "nickname", "网易云用户");
        userId = u64Of(root, "userId");
    } else if (code != 800 && code != 801 && code != 802) {
        error = jsonMessageOf(json, "二维码登录失败");
        result = -1;
    }
    cJSON_Delete(root);
    return result;
}

bool parseLoginResponse(const char* json, std::string& cookie, std::string& nickname,
                        uint64_t& userId, std::string& error) {
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        error = "登录响应不是有效 JSON";
        return false;
    }
    bool ok = false;
    if (codeOf(root, 0) != 200) {
        error = jsonMessageOf(json, "登录失败，请检查手机号和密码");
    } else {
        cookie = strOf(root, "cookie", "");
        const cJSON* profile = objectOf(root, "profile");
        nickname = strOf(profile, "nickname", "网易云用户");
        userId = u64Of(profile, "userId");
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

bool parseStatusResponse(const char* json, bool& hasProfile, std::string& nickname,
                         uint64_t& userId) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return false;
    const cJSON* profile = objectOf(objectOf(root, "data"), "profile");
    if (!profile) profile = objectOf(root, "profile");
    hasProfile = profile != nullptr;
    if (hasProfile) {
        nickname = strOf(profile, "nickname", "网易云用户");
        userId = u64Of(profile, "userId");
    }
    cJSON_Delete(root);
    return true;
}

}  // namespace lz
