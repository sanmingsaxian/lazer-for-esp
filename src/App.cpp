#include "App.h"

LazerApp App;

void LazerApp::begin() {
    Settings.begin();
    lyricsMode = Settings.showLyrics;
    M5Cardputer.Display.setBrightness(Settings.brightness);
    Audio.setVolume(Settings.volume);
    Audio.setPlayMode(Settings.playMode);
}

void LazerApp::update() {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        handleKey(M5Cardputer.Keyboard.keysState());
    }
    if (toast.length() && millis() > toastUntil) toast = "";
}

std::vector<int> LazerApp::favoriteIndices() const {
    std::vector<int> result;
    for (size_t i = 0; i < Library.count(); ++i) {
        if (Settings.isFavorite(Library.track(i)->seed)) result.push_back((int)i);
    }
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
        if (!favorites.size()) return -1;
        const int index = constrain(selected, 0, (int)favorites.size() - 1);
        return favorites[index];
    }
    return constrain(selected, 0, (int)Library.count() - 1);
}

void LazerApp::notify(const String& message, uint16_t milliseconds) {
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

void LazerApp::handleKey(const Keyboard_Class::KeysState& keys) {
    char key = keys.word.size() ? (char)tolower(keys.word[0]) : 0;
    if (helpOpen) {
        if ((key == '`') || keys.enter || keys.space || key == 'h') helpOpen = false;
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
    if ((key == '`')) {
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

void LazerApp::handleBrowserKey(const Keyboard_Class::KeysState& keys, char key) {
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
        if (Audio.currentTrack() >= 0) Audio.toggle();
        else playSelected();
        return;
    }
    if (key == 'm') {
        if (Audio.currentTrack() < 0) playSelected();
        if (Audio.currentTrack() >= 0) playerOpen = true;
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

void LazerApp::handlePlayerKey(const Keyboard_Class::KeysState& keys, char key) {
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

void LazerApp::handleQueueKey(const Keyboard_Class::KeysState& keys, char key) {
    if ((key == '`') || key == 'q') {
        queueOpen = false;
        return;
    }
    if ((keys.fn && key == ';') || (keys.fn && key == '.') || key == 'w' || key == 's') {
        selectRelative(((keys.fn && key == ';') || key == 'w') ? -1 : 1);
        return;
    }
    if (keys.enter) {
        int index = selectedTrack();
        if (index >= 0) {
            Audio.play(index);
            queueOpen = false;
            playerOpen = true;
        }
    }
}

void LazerApp::handleSettingsKey(const Keyboard_Class::KeysState& keys, char key) {
    if ((keys.fn && key == ';') || (keys.fn && key == '.') || key == 'w' || key == 's') {
        int delta = ((keys.fn && key == ';') || key == 'w') ? -1 : 1;
        settingsCursor = (settingsCursor + delta + 6) % 6;
        return;
    }
    if ((keys.fn && key == ',') || (keys.fn && key == '/')) {
        if (settingsCursor == 4) {
            int value = Settings.brightness + ((keys.fn && key == '/') ? 20 : -20);
            Settings.brightness = constrain(value, 40, 255);
            M5Cardputer.Display.setBrightness(Settings.brightness);
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
        case 0:
            Settings.dark = !Settings.dark;
            break;
        case 1:
            Settings.showLyrics = !Settings.showLyrics;
            lyricsMode = Settings.showLyrics;
            break;
        case 2:
            Settings.reduceMotion = !Settings.reduceMotion;
            break;
        case 3:
            cyclePlayMode();
            break;
        case 4:
            Settings.brightness = Settings.brightness >= 240 ? 80 : Settings.brightness + 20;
            M5Cardputer.Display.setBrightness(Settings.brightness);
            break;
        case 5:
            rescan();
            break;
    }
    Settings.save();
}

void LazerApp::selectRelative(int delta) {
    int count = listCount();
    if (!count) return;
    selected = (selected + delta + count) % count;
}

void LazerApp::playSelected() {
    int index = selectedTrack();
    if (index >= 0) {
        Audio.play(index);
        playerOpen = true;
    } else {
        notify("请在 SD 卡中添加 MP3");
    }
}

void LazerApp::toggleSelectedFavorite() {
    int index = selectedTrack();
    const Track* track = Library.track(index);
    if (!track) return;
    bool favorite = Settings.toggleFavorite(track->seed);
    notify(favorite ? "已加入我喜欢" : "已取消喜欢");
    if (screen == Screen::Favorites && !favorite) {
        int count = listCount();
        if (selected >= count) selected = max(0, count - 1);
    }
}

void LazerApp::changeVolume(int delta) {
    int value = (int)Settings.volume + delta;
    Settings.volume = constrain(value, 0, 20);
    Audio.setVolume(Settings.volume);
    Settings.save();
    notify("音量 " + String(Settings.volume * 5) + "%", 700);
}

void LazerApp::rescan() {
    notify("正在扫描曲库…", 4000);
    Audio.eject();
    delay(40);
    bool ok = Library.ready() ? Library.rescan() : Library.begin();
    selected = 0;
    notify(ok ? "找到 " + String(Library.count()) + " 首音乐" : "未检测到 SD 卡");
}
