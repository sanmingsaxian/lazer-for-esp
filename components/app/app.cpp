#include "app.h"
#include "audio_engine.h"
#include "board.h"
#include "library.h"
#include "netease.h"
#include "settings.h"
#include "strutil.h"

#include <cctype>
#include <cstdio>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace lz {

LazerApp App;

namespace {
int clampInt(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}
bool isDigitChar(char c) { return c >= '0' && c <= '9'; }
}  // namespace

void LazerApp::begin() {
    bootResetCode = (int)esp_reset_reason();
    bootReset = board_reset_reason_name(bootResetCode);
    printf("RESET_REASON=%d (%s)\n", bootResetCode, bootReset.c_str());
    Settings.begin();
    lyricsMode = Settings.showLyrics;
    board_set_brightness(Settings.brightness);
    Audio.setVolume(Settings.volume);
    Audio.setPlayMode(Settings.playMode);
}

void LazerApp::update() {
    keyboard_update();
    if (keyboard_changed() && keyboard_pressed()) handleKey(keyboard_state());
    if (neteaseOpen && neteaseView == NeteaseView::Qr && neteaseQrActive && !neteaseBusy &&
        (int32_t)(millis() - _nextQrPoll) >= 0) {
        neteaseBusy = true;
        std::string error;
        const int code = Netease.qrCheck(error);
        neteaseBusy = false;
        neteaseQrCode = code;
        if (code == 801 || code == 802) {
            neteaseQrStatus = code == 801 ? "等待扫码" : "已扫码，请在手机上确认";
            _nextQrPoll = millis() + 2200;
        } else if (code == 803) {
            neteaseQrActive = false;
            neteaseQrStatus = "登录成功：" + Netease.nickname();
            neteaseMessage = "Cookie 已保存到设备";
        } else {
            neteaseQrActive = false;
            neteaseQrStatus = code == 800 ? "二维码已过期，按 ENTER 重新生成"
                                          : (!error.empty() ? error : "二维码登录失败");
        }
    }
    if (!toast.empty() && millis() > toastUntil) toast.clear();
}

std::vector<int> LazerApp::favoriteIndices() const {
    std::vector<int> result;
    for (size_t i = 0; i < Library.count(); ++i)
        if (Settings.isFavorite(Library.track(i)->seed)) result.push_back((int)i);
    return result;
}

int LazerApp::listCount() const {
    if (screen == Screen::Library) return (int)Library.count();
    if (screen == Screen::Favorites) return (int)favoriteIndices().size();
    return (int)Library.count();
}

int LazerApp::selectedTrack() const {
    if (!Library.count()) return -1;
    if (screen == Screen::Favorites) {
        auto favorites = favoriteIndices();
        if (favorites.empty()) return -1;
        const int index = clampInt(selected, 0, (int)favorites.size() - 1);
        return favorites[index];
    }
    return clampInt(selected, 0, (int)Library.count() - 1);
}

void LazerApp::notify(const std::string& message, uint16_t milliseconds) {
    toast = message;
    toastUntil = millis() + milliseconds;
}

void LazerApp::cyclePlayMode() {
    Settings.playMode = (PlayMode)(((uint8_t)Settings.playMode + 1) % 3);
    Audio.setPlayMode(Settings.playMode);
    Settings.save();
    const char* names[] = {"顺序循环", "单曲循环", "随机播放"};
    notify(names[(uint8_t)Settings.playMode]);
}

void LazerApp::handleKey(const KeyState& keys) {
    const char key = !keys.word.empty() ? (char)tolower((unsigned char)keys.word[0]) : 0;
    if (textEditOpen) {
        handleTextEditKey(keys);
        return;
    }
    if (neteaseOpen) {
        handleNeteaseKey(keys, key);
        return;
    }
    if (helpOpen) {
        if (key == '`' || keys.enter || keys.space || key == 'h') helpOpen = false;
        return;
    }
    if (key == 'h') {
        helpOpen = true;
        return;
    }
    if (queueOpen) {
        handleQueueKey(keys, key);
        return;
    }
    if (key == '`') {
        if (playerOpen) playerOpen = false;
        return;
    }
    if (playerOpen) {
        handlePlayerKey(keys, key);
        return;
    }
    if (keys.tab || (key >= '1' && key <= '4')) {
        screen = keys.tab ? (Screen)(((uint8_t)screen + 1) % 4) : (Screen)(key - '1');
        selected = 0;
        return;
    }
    if (screen == Screen::Settings) {
        handleSettingsKey(keys, key);
        if (screen == Screen::Settings) return;
    }
    handleBrowserKey(keys, key);
}

void LazerApp::handleBrowserKey(const KeyState& keys, char key) {
    if (keys.tab || (keys.fn && key == '/') || key == 'd') {
        screen = (Screen)(((uint8_t)screen + 1) % 4);
        selected = 0;
        return;
    }
    if ((keys.fn && key == ',') || key == 'a') {
        screen = (Screen)(((uint8_t)screen + 3) % 4);
        selected = 0;
        return;
    }
    if (key >= '1' && key <= '4') {
        screen = (Screen)(key - '1');
        selected = 0;
        return;
    }
    if ((keys.fn && key == ';') || (keys.fn && key == '.') || key == 'w' || key == 's') {
        selectRelative(((keys.fn && key == ';') || key == 'w') ? -1 : 1);
        return;
    }
    if (keys.enter) {
        playSelected();
        return;
    }
    if (keys.space) {
        if (Audio.hasSelection()) Audio.toggle();
        else playSelected();
        return;
    }
    if (key == 'm') {
        if (!Audio.hasSelection()) playSelected();
        if (Audio.hasSelection()) playerOpen = true;
        return;
    }
    if (key == 'q') {
        if (Library.count()) queueOpen = true;
        return;
    }
    if (key == 'f') {
        toggleSelectedFavorite();
        return;
    }
    if (key == 'r') {
        cyclePlayMode();
        return;
    }
    if (key == 'n') Audio.next();
    else if (key == 'p') Audio.previous();
    else if (key == '=' || key == '+') changeVolume(1);
    else if (key == '-') changeVolume(-1);
}

void LazerApp::handlePlayerKey(const KeyState& keys, char key) {
    if (key == 'm') {
        playerOpen = false;
        return;
    }
    if (keys.space) Audio.toggle();
    else if ((keys.fn && key == ',') || key == 'a') Audio.seek(-5);
    else if ((keys.fn && key == '/') || key == 'd') Audio.seek(5);
    else if ((keys.fn && key == ';') || key == 'w') changeVolume(1);
    else if ((keys.fn && key == '.') || key == 's') changeVolume(-1);
    else if (key == 'n') Audio.next();
    else if (key == 'p') Audio.previous();
    else if (key == 'l') {
        lyricsMode = !lyricsMode;
        Settings.showLyrics = lyricsMode;
        Settings.save();
    } else if (key == 'q') queueOpen = true;
    else if (key == 'f') {
        const Track* track = Library.track(Audio.currentTrack());
        if (track) notify(Settings.toggleFavorite(track->seed) ? "已加入我喜欢" : "已取消喜欢");
    } else if (key == 'r') cyclePlayMode();
    else if (key == '=' || key == '+') changeVolume(1);
    else if (key == '-') changeVolume(-1);
}

void LazerApp::handleQueueKey(const KeyState& keys, char key) {
    if (key == '`' || key == 'q') {
        queueOpen = false;
        return;
    }
    if ((keys.fn && key == ';') || (keys.fn && key == '.') || key == 'w' || key == 's') {
        selectRelative(((keys.fn && key == ';') || key == 'w') ? -1 : 1);
        return;
    }
    if (keys.enter) {
        const int index = selectedTrack();
        if (index >= 0) {
            Audio.play(index);
            queueOpen = false;
            playerOpen = true;
        }
    }
}

void LazerApp::handleSettingsKey(const KeyState& keys, char key) {
    if ((keys.fn && key == ';') || (keys.fn && key == '.') || key == 'w' || key == 's') {
        const int delta = ((keys.fn && key == ';') || key == 'w') ? -1 : 1;
        settingsCursor = (settingsCursor + delta + 14) % 14;
        return;
    }
    if ((keys.fn && key == ',') || (keys.fn && key == '/')) {
        if (settingsCursor == 4) {
            const int value = Settings.brightness + ((keys.fn && key == '/') ? 20 : -20);
            Settings.brightness = (uint8_t)clampInt(value, 40, 255);
            board_set_brightness(Settings.brightness);
            Settings.save();
        }
        return;
    }
    if (!keys.enter) {
        if (key == '=' || key == '+') changeVolume(1);
        else if (key == '-') changeVolume(-1);
        return;
    }
    switch (settingsCursor) {
        case 0: Settings.dark = !Settings.dark; break;
        case 1:
            Settings.showLyrics = !Settings.showLyrics;
            lyricsMode = Settings.showLyrics;
            break;
        case 2: Settings.reduceMotion = !Settings.reduceMotion; break;
        case 3: cyclePlayMode(); break;
        case 4:
            Settings.brightness = Settings.brightness >= 240 ? 80 : Settings.brightness + 20;
            board_set_brightness(Settings.brightness);
            break;
        case 5: rescan(); break;
        case 7: openNetease(); break;
        case 8: beginTextEdit(TextEditTarget::NeteaseApi); break;
        case 9: beginTextEdit(TextEditTarget::ApSsid); break;
        case 10: beginTextEdit(TextEditTarget::ApPassword); break;
        case 11: beginTextEdit(TextEditTarget::StaSsid); break;
        case 12: beginTextEdit(TextEditTarget::StaPassword); break;
        case 13:
            Settings.save();
            vTaskDelay(pdMS_TO_TICKS(80));
            esp_restart();
            break;
        default: break;
    }
    Settings.save();
}

void LazerApp::beginTextEdit(TextEditTarget target) {
    textEditOpen = true;
    textEditTarget = target;
    textEditSecret = target == TextEditTarget::ApPassword || target == TextEditTarget::StaPassword ||
                     target == TextEditTarget::NeteasePassword;
    switch (target) {
        case TextEditTarget::ApPassword: textEditValue = Settings.apPassword; break;
        case TextEditTarget::StaSsid: textEditValue = Settings.staSsid; break;
        case TextEditTarget::StaPassword: textEditValue = Settings.staPassword; break;
        case TextEditTarget::NeteaseApi: textEditValue = Settings.neteaseApi; break;
        case TextEditTarget::NeteaseCountry: textEditValue = neteaseCountry; break;
        case TextEditTarget::NeteasePhone: textEditValue = neteasePhone; break;
        case TextEditTarget::NeteasePassword: textEditValue = neteasePassword; break;
        default: textEditValue = Settings.apSsid; break;
    }
}

void LazerApp::handleTextEditKey(const KeyState& keys) {
    if (!keys.word.empty() && keys.word[0] == '`') {
        textEditOpen = false;
        textEditValue.clear();
        notify("已取消修改");
        return;
    }
    const bool password = textEditTarget == TextEditTarget::ApPassword ||
                          textEditTarget == TextEditTarget::StaPassword;
    const bool station = textEditTarget == TextEditTarget::StaSsid ||
                         textEditTarget == TextEditTarget::StaPassword;
    const bool neteaseEdit = (uint8_t)textEditTarget >= (uint8_t)TextEditTarget::NeteaseApi;
    size_t maximum = password ? 63 : 32;
    if (textEditTarget == TextEditTarget::NeteaseApi) maximum = 160;
    else if (textEditTarget == TextEditTarget::NeteaseCountry) maximum = 5;
    else if (textEditTarget == TextEditTarget::NeteasePhone) maximum = 20;
    else if (textEditTarget == TextEditTarget::NeteasePassword) maximum = 128;

    if (keys.enter) {
        std::string value = textEditValue;
        if (!textEditSecret) value = trim(value);
        if (neteaseEdit) {
            std::string error;
            bool ok = true;
            if (textEditTarget == TextEditTarget::NeteaseApi) {
                ok = Netease.configure(value, error);
            } else if (textEditTarget == TextEditTarget::NeteaseCountry) {
                ok = value.size() >= 1 && value.size() <= 5;
                for (char c : value) ok = ok && isDigitChar(c);
                if (ok) neteaseCountry = value;
                else error = "地区码应为 1–5 位数字";
            } else if (textEditTarget == TextEditTarget::NeteasePhone) {
                ok = value.size() >= 5 && value.size() <= 20;
                for (char c : value) ok = ok && isDigitChar(c);
                if (ok) neteasePhone = value;
                else error = "手机号应为 5–20 位数字";
            } else {
                ok = value.size() >= 1 && value.size() <= 128;
                if (ok) neteasePassword = value;
                else error = "网易云密码不能为空";
            }
            if (!ok) {
                notify(error, 3000);
                return;
            }
            textEditOpen = false;
            textEditValue.clear();
            notify(textEditTarget == TextEditTarget::NeteaseApi ? "API 地址已保存" : "输入已保存");
            return;
        }
        const size_t minimum = password && !station ? 8 : (password ? 0 : 1);
        const bool shortStationPassword = station && password && !value.empty() && value.size() < 8;
        if (value.size() < minimum || value.size() > maximum || shortStationPassword) {
            notify(password ? (station ? "Wi-Fi 密码留空或 8–63 位" : "AP 密码需为 8–63 位")
                            : "名称需为 1–32 字节", 2600);
            return;
        }
        switch (textEditTarget) {
            case TextEditTarget::ApPassword: Settings.apPassword = value; break;
            case TextEditTarget::StaSsid: Settings.staSsid = value; break;
            case TextEditTarget::StaPassword: Settings.staPassword = value; break;
            default: Settings.apSsid = value; break;
        }
        Settings.save();
        textEditOpen = false;
        textEditValue.clear();
        notify(station ? "Wi-Fi 已保存，重启后连接" : "AP 配置已保存，重启后生效", 3000);
        return;
    }
    if (keys.del && !textEditValue.empty()) {
        size_t count = 1;
        while (textEditValue.size() > count &&
               ((uint8_t)textEditValue[textEditValue.size() - count] & 0xc0) == 0x80)
            ++count;
        textEditValue.erase(textEditValue.size() - count, count);
    }
    bool wordHasSpace = false;
    for (char c : keys.word) {
        if (c == ' ') wordHasSpace = true;
        if ((uint8_t)c >= 32 && (uint8_t)c != 127 && textEditValue.size() < maximum)
            textEditValue += c;
    }
    if (keys.space && !wordHasSpace && textEditTarget != TextEditTarget::NeteaseApi &&
        textEditTarget != TextEditTarget::NeteaseCountry &&
        textEditTarget != TextEditTarget::NeteasePhone && textEditValue.size() < maximum)
        textEditValue += ' ';
}

void LazerApp::openNetease(NeteaseView view) {
    neteaseOpen = true;
    neteaseView = view;
    neteaseCursor = 0;
    neteaseMessage = Netease.loggedIn() ? "已登录：" + Netease.nickname() : "请选择登录方式";
    if (view == NeteaseView::Qr) startNeteaseQr();
}

void LazerApp::startNeteaseQr() {
    neteaseView = NeteaseView::Qr;
    neteaseBusy = true;
    neteaseQrActive = false;
    neteaseQrCode = 0;
    neteaseQrUrl.clear();
    neteaseQrStatus = "正在获取二维码…";
    std::string image, error;
    const bool ok = Netease.qrBegin(image, neteaseQrUrl, error);
    image.clear();  // device draws qrurl locally; drop the base64 image immediately
    neteaseBusy = false;
    neteaseQrActive = ok;
    neteaseQrCode = ok ? 801 : -1;
    neteaseQrStatus = ok ? "等待扫码" : error;
    _nextQrPoll = millis() + 2200;
}

void LazerApp::loginNeteasePhone() {
    neteaseBusy = true;
    neteaseMessage = "正在登录…";
    std::string error;
    const bool ok = Netease.loginPhone(neteaseCountry, neteasePhone, neteasePassword, error);
    neteasePassword.clear();  // never persist or retain the account password
    neteaseBusy = false;
    if (ok) {
        neteaseView = NeteaseView::Menu;
        neteaseCursor = 0;
        neteaseMessage = "已登录：" + Netease.nickname();
    } else {
        neteaseMessage = error;
    }
}

void LazerApp::handleNeteaseKey(const KeyState& keys, char key) {
    if (neteaseBusy) return;
    if (key == '`') {
        neteaseOpen = false;
        neteaseQrActive = false;
        neteasePassword.clear();
        neteaseQrUrl.clear();
        return;
    }
    if (neteaseView == NeteaseView::Qr) {
        if ((keys.enter || key == 'r') && !neteaseQrActive) {
            if (neteaseQrCode == 803) openNetease();
            else startNeteaseQr();
        }
        return;
    }
    const int count = neteaseView == NeteaseView::Phone ? 5 : 6;
    if ((keys.fn && key == ';') || (keys.fn && key == '.') || key == 'w' || key == 's') {
        const int delta = ((keys.fn && key == ';') || key == 'w') ? -1 : 1;
        neteaseCursor = (neteaseCursor + delta + count) % count;
        return;
    }
    if (!keys.enter) return;
    if (neteaseView == NeteaseView::Phone) {
        switch (neteaseCursor) {
            case 0: beginTextEdit(TextEditTarget::NeteaseCountry); break;
            case 1: beginTextEdit(TextEditTarget::NeteasePhone); break;
            case 2: beginTextEdit(TextEditTarget::NeteasePassword); break;
            case 3: loginNeteasePhone(); break;
            default: openNetease(); break;
        }
        return;
    }
    std::string error;
    switch (neteaseCursor) {
        case 0: startNeteaseQr(); break;
        case 1:
            neteaseView = NeteaseView::Phone;
            neteaseCursor = 0;
            neteaseMessage = "密码仅用于本次登录，不会保存";
            break;
        case 2:
            neteaseBusy = true;
            if (Netease.refreshStatus(error))
                neteaseMessage = Netease.loggedIn() ? "已登录：" + Netease.nickname() : "当前未登录";
            else neteaseMessage = error;
            neteaseBusy = false;
            break;
        case 3:
            Netease.logout(error);
            neteaseMessage = "已退出登录并清除本机 Cookie";
            break;
        case 4: beginTextEdit(TextEditTarget::NeteaseApi); break;
        default: neteaseOpen = false; break;
    }
}

void LazerApp::selectRelative(int delta) {
    const int count = listCount();
    if (!count) return;
    selected = (selected + delta + count) % count;
}

void LazerApp::playSelected() {
    const int index = selectedTrack();
    if (index >= 0) {
        Audio.play(index);
        playerOpen = true;
    } else {
        notify("请在 SD 卡中添加 MP3");
    }
}

void LazerApp::toggleSelectedFavorite() {
    const int index = selectedTrack();
    const Track* track = Library.track(index);
    if (!track) return;
    const bool favorite = Settings.toggleFavorite(track->seed);
    notify(favorite ? "已加入我喜欢" : "已取消喜欢");
    if (screen == Screen::Favorites && !favorite) {
        const int count = listCount();
        if (selected >= count) selected = count > 0 ? count - 1 : 0;
    }
}

void LazerApp::changeVolume(int delta) {
    const int value = (int)Settings.volume + delta;
    Settings.volume = (uint8_t)clampInt(value, 0, 20);
    Audio.setVolume(Settings.volume);
    Settings.save();
    notify("音量 " + std::to_string(Settings.volume * 5) + "%", 700);
}

void LazerApp::rescan() {
    notify("正在扫描曲库…", 4000);
    const bool locked = Audio.storageBegin();
    bool ok = false;
    if (locked) ok = Library.ready() ? Library.rescan() : Library.begin();
    Audio.storageEnd();
    selected = 0;
    notify(!locked ? "音频未停止，请稍后重试"
                   : (ok ? "找到 " + std::to_string(Library.count()) + " 首音乐" : "未检测到 SD 卡"));
}

}  // namespace lz
