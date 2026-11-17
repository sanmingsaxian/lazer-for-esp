#include "UI.h"
#include "WebControl.h"
#include <math.h>

LazerUI UI;

namespace {
constexpr uint32_t MUSIC_RED = 0xfa2d48;
constexpr uint32_t DARK_CARD = 0x1c1c1e;
constexpr uint32_t LIGHT_CARD = 0xffffff;

uint16_t hsvColor(float h, float s, float v) {
    h = h - floorf(h);
    float r, g, b;
    int i = (int)floorf(h * 6.0f);
    float f = h * 6.0f - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    uint16_t rr = (uint16_t)(r * 31.0f);
    uint16_t gg = (uint16_t)(g * 63.0f);
    uint16_t bb = (uint16_t)(b * 31.0f);
    return (rr << 11) | (gg << 5) | bb;
}
}

void LazerUI::begin() {
    // M5GFX 0.3.0 applies a panel rotation offset of 1 on Cardputer.
    // Rotation 0 gives an upright 240x135 landscape on the real v1.1 unit;
    // rotation 1 turns the whole canvas 90 degrees away from the keyboard.
    M5Cardputer.Display.setRotation(0);
    canvas.setColorDepth(16);
    canvas.createSprite(240, 135);
    canvas.setTextWrap(false);
    _playerMotion.snap(0);
    _contentMotion.snap(1);
    _tabMotion.snap(0);
    _lyricMotion.snap(1);
}

uint16_t LazerUI::color(uint8_t r, uint8_t g, uint8_t b) const {
    return M5Cardputer.Display.color565(r, g, b);
}

uint16_t LazerUI::blend(uint16_t a, uint16_t b, float t) const {
    t = constrain(t, 0.0f, 1.0f);
    uint16_t ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    uint16_t br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    uint16_t r = ar + (br - ar) * t;
    uint16_t g = ag + (bg - ag) * t;
    uint16_t bl = ab + (bb - ab) * t;
    return (r << 11) | (g << 5) | bl;
}

uint16_t LazerUI::background() const { return Settings.dark ? color(0, 0, 0) : color(242, 242, 247); }
uint16_t LazerUI::card() const { return Settings.dark ? color(28, 28, 30) : color(255, 255, 255); }
uint16_t LazerUI::label() const { return Settings.dark ? color(255, 255, 255) : color(0, 0, 0); }
uint16_t LazerUI::secondary() const { return Settings.dark ? color(158, 158, 165) : color(105, 105, 115); }
uint16_t LazerUI::accent() const { return color(250, 45, 72); }

void LazerUI::setFont(FontRole role, uint16_t foreground) {
    if (role == FontRole::Title) canvas.setFont(&fonts::efontCN_16);
    else if (role == FontRole::Body) canvas.setFont(&fonts::efontCN_12);
    else canvas.setFont(&fonts::efontCN_10);
    canvas.setTextSize(1);
    canvas.setTextColor(foreground);
}

void LazerUI::text(const String& value, int x, int y, int maxWidth, uint16_t foreground,
                   FontRole role, int datum) {
    setFont(role, foreground);
    canvas.setTextDatum(datum);
    int clipX = x;
    if (datum == TC_DATUM) clipX = x - maxWidth / 2;
    else if (datum == TR_DATUM) clipX = x - maxWidth;
    canvas.setClipRect(clipX, y, maxWidth, role == FontRole::Title ? 20 : (role == FontRole::Body ? 16 : 13));
    canvas.drawString(value, x, y);
    canvas.clearClipRect();
}

void LazerUI::line(int x, int y, int w, uint16_t tint) {
    canvas.drawFastHLine(x, y, w, tint);
}

String LazerUI::timeString(uint32_t seconds) const {
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%lu:%02lu", (unsigned long)(seconds / 60),
             (unsigned long)(seconds % 60));
    return String(buffer);
}

const Track* LazerUI::currentTrack() const {
    return Audio.currentTrack() >= 0 ? Library.track(Audio.currentTrack()) : nullptr;
}

void LazerUI::palette(uint32_t seed, uint16_t* colors) const {
    float h = ((seed & 0xff) / 255.0f);
    colors[0] = hsvColor(h, 0.72f, 0.84f);
    colors[1] = hsvColor(h + 0.09f, 0.64f, 0.68f);
    colors[2] = hsvColor(h + 0.52f, 0.58f, 0.58f);
}

void LazerUI::drawCover(int x, int y, int size, uint32_t seed, bool playing) {
    uint16_t p[3];
    palette(seed, p);
    canvas.fillRoundRect(x, y, size, size, max(3, size / 10), p[0]);
    canvas.setClipRect(x, y, size, size);
    for (int i = 0; i < size; i += 4) {
        float t = (float)i / size;
        canvas.fillRect(x + 2, y + i, size - 4, min(4, size - i), blend(p[0], p[1], t * 0.75f));
    }
    canvas.fillCircle(x + size * 0.78f, y + size * 0.18f, size * 0.36f, blend(p[1], p[2], 0.45f));
    canvas.fillCircle(x + size * 0.12f, y + size * 0.95f, size * 0.52f, blend(p[0], p[2], 0.55f));
    int cx = x + size / 2;
    int cy = y + size / 2;
    uint16_t white = color(255, 245, 248);
    int unit = max(1, size / 24);
    canvas.fillRect(cx, cy - size / 4, unit * 2, size / 2, white);
    canvas.fillRect(cx, cy - size / 4, size / 5, unit * 3, white);
    canvas.fillCircle(cx - size / 8, cy + size / 5, size / 8, white);
    if (playing && size >= 40) {
        for (int i = 0; i < 3; ++i) {
            int h = 3 + ((millis() / 180 + i * 2) % 7);
            canvas.fillRect(x + size - 15 + i * 4, y + size - 5 - h, 2, h, white);
        }
    }
    canvas.clearClipRect();
}

void LazerUI::drawMesh(uint32_t seed, int yOffset) {
    uint16_t p[3];
    palette(seed, p);
    uint16_t dark = Settings.dark ? color(8, 7, 10) : color(238, 235, 240);
    canvas.fillRect(0, yOffset, 240, 135 - yOffset, dark);
    float time = Settings.reduceMotion ? 0.0f : millis() / 4200.0f;
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 6; ++col) {
            float wave = 0.5f + 0.5f * sinf(time + col * 0.83f + row * 0.61f + (seed & 15));
            uint16_t tint = blend(p[col % 3], p[(col + row + 1) % 3], wave);
            float strength = 0.28f + 0.22f * (1.0f - fabsf(col - 2.5f) / 3.0f);
            canvas.fillRect(col * 40, yOffset + row * 20, 40, 20, blend(dark, tint, strength));
        }
    }
}

void LazerUI::drawStatus() {
    text("Lazer", 7, 1, 44, secondary(), FontRole::Tiny);
    int level = M5Cardputer.Power.getBatteryLevel();
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    uint16_t tint = level < 20 ? accent() : secondary();
    canvas.drawRect(216, 3, 16, 7, tint);
    canvas.fillRect(232, 5, 2, 3, tint);
    canvas.fillRect(218, 5, max(1, level * 12 / 100), 3, tint);
}

void LazerUI::render() {
    uint32_t now = millis();
    if (now - _lastFrame < (App.playerOpen ? 33 : 50)) return;
    _lastFrame = now;

    if (App.playerOpen != _lastPlayerOpen) {
        _playerMotion.retarget(App.playerOpen ? 1.0f : 0.0f, 0.46f, 0.0f, Settings.reduceMotion);
        _lastPlayerOpen = App.playerOpen;
    }
    if (App.screen != _lastScreen) {
        _contentMotion.snap(0);
        _contentMotion.retarget(1, 0.32f, 0.08f, Settings.reduceMotion);
        _tabMotion.retarget((float)(uint8_t)App.screen, 0.35f, 0.12f, Settings.reduceMotion);
        _lastScreen = App.screen;
    }
    _playerMotion.update(now);
    _contentMotion.update(now);
    _tabMotion.update(now);
    _lyricMotion.update(now);

    float playerAmount = constrain(_playerMotion.value, 0.0f, 1.0f);
    if (playerAmount < 0.999f) drawBrowse((1.0f - _contentMotion.value) * 12.0f);
    if (playerAmount > 0.001f) drawNowPlaying((1.0f - easeInOutCubic(playerAmount)) * 135.0f);
    if (App.queueOpen) drawQueue();
    if (App.helpOpen) drawHelp();
    if (App.textEditOpen) drawTextEditor();
    if (App.toast.length()) drawToast();
    canvas.pushSprite(0, 0);
}

void LazerUI::drawBrowse(int xOffset) {
    canvas.fillSprite(background());
    drawStatus();
    switch (App.screen) {
        case Screen::Home: drawHome(xOffset); break;
        case Screen::Library: drawLibrary(xOffset, false); break;
        case Screen::Favorites: drawLibrary(xOffset, true); break;
        case Screen::Settings: drawSettings(xOffset); break;
    }
    if (Audio.currentTrack() >= 0) drawMiniPlayer();
    drawTabs();
}

void LazerUI::drawHome(int xOffset) {
    text("现在就听", 8 + xOffset, 13, 160, label(), FontRole::Title);
    const Track* current = currentTrack();
    uint16_t hero = blend(card(), accent(), Settings.dark ? 0.16f : 0.08f);
    canvas.fillRoundRect(7 + xOffset, 34, 226, 34, 9, hero);
    if (Library.count()) {
        const Track* track = current ? current : Library.track(0);
        drawCover(11 + xOffset, 38, 26, track->seed, Audio.playing());
        text(current ? "继续播放" : "为 Cardputer 打造", 43 + xOffset, 37, 128, accent(), FontRole::Tiny);
        text(track->title, 43 + xOffset, 49, 145, label(), FontRole::Body);
        drawPlayIcon(213 + xOffset, 45, Audio.playing(), accent(), 1);
    } else {
        text("将 MP3 与同名 LRC 放入 SD 卡", 15 + xOffset, 43, 210, secondary(), FontRole::Body);
    }
    text("快速选取", 9 + xOffset, 70, 100, secondary(), FontRole::Tiny);
    int quickRows = Audio.currentTrack() >= 0 ? 1 : 2;
    for (int row = 0; row < quickRows && row < (int)Library.count(); ++row) {
        const Track* track = Library.track(row);
        bool selected = App.selected == row;
        if (selected) canvas.fillRoundRect(7 + xOffset, 82 + row * 15, 226, 14, 4,
                                           blend(card(), accent(), 0.17f));
        canvas.fillCircle(15 + xOffset, 89 + row * 15, 3, row ? accent() : secondary());
        text(track->title, 24 + xOffset, 82 + row * 15, 165,
             selected ? label() : secondary(), FontRole::Tiny);
        if (Settings.isFavorite(track->seed)) drawHeart(220 + xOffset, 86 + row * 15, true, accent());
    }
}

void LazerUI::drawLibrary(int xOffset, bool favorites) {
    text(favorites ? "我喜欢" : "音乐库", 8 + xOffset, 13, 150, label(), FontRole::Title);
    int count = App.listCount();
    text(String(count) + " 首", 191 + xOffset, 18, 40, secondary(), FontRole::Tiny);
    if (!favorites) {
        canvas.fillRoundRect(7 + xOffset, 34, 226, 18, 6, card());
        canvas.fillRoundRect(11 + xOffset, 38, 10, 10, 3, accent());
        text("本地音乐", 28 + xOffset, 35, 90, label(), FontRole::Body);
        text(Library.ready() ? "SD 卡" : "未插卡", 181 + xOffset, 38, 44, secondary(), FontRole::Tiny);
    }
    if (!count) {
        text(favorites ? "按 F 收藏喜欢的歌曲" : "没有找到 MP3 文件", 12 + xOffset,
             favorites ? 49 : 61, 216, secondary(), FontRole::Body);
        return;
    }
    auto favoriteList = favorites ? App.favoriteIndices() : std::vector<int>();
    int startY = favorites ? 35 : 55;
    int rows = favorites ? 3 : 2;
    int first = constrain(App.selected - rows + 1, 0, max(0, count - rows));
    for (int row = 0; row < rows && first + row < count; ++row) {
        int listIndex = first + row;
        int trackIndex = favorites ? favoriteList[listIndex] : listIndex;
        const Track* track = Library.track(trackIndex);
        int y = startY + row * 17;
        bool selected = listIndex == App.selected;
        bool playing = trackIndex == Audio.currentTrack();
        if (selected) canvas.fillRoundRect(6 + xOffset, y - 2, 228, 16, 5,
                                           blend(card(), accent(), 0.18f));
        else if (row) line(35 + xOffset, y - 3, 196, blend(background(), secondary(), 0.25f));
        canvas.fillRoundRect(10 + xOffset, y, 12, 12, 3, blend(card(), accent(), playing ? 0.85f : 0.20f));
        if (playing) drawPlayIcon(13 + xOffset, y + 3, Audio.playing(), color(255, 255, 255));
        text(track->title, 28 + xOffset, y - 1, 147, selected || playing ? label() : secondary(), FontRole::Body);
        if (Settings.isFavorite(track->seed)) drawHeart(188 + xOffset, y + 2, true, accent());
        text(timeString(track->duration), 207 + xOffset, y + 1, 25, secondary(), FontRole::Tiny);
    }
    if (count > rows) {
        int trackHeight = rows * 17;
        int barY = startY + (trackHeight - 16) * App.selected / max(1, count - 1);
        canvas.fillRoundRect(236 + xOffset, barY, 2, 12, 1, secondary());
    }
}

void LazerUI::drawSettings(int xOffset) {
    text("设置", 8 + xOffset, 13, 150, label(), FontRole::Title);
    const char* names[10] = {"深色外观", "显示歌词", "减少动态效果", "播放模式", "屏幕亮度",
                             "重新扫描曲库", "WebUI 地址", "AP 名称", "AP 密码", "重启应用配置"};
    String values[10] = {Settings.dark ? "开启" : "关闭", Settings.showLyrics ? "开启" : "关闭",
                        Settings.reduceMotion ? "开启" : "关闭",
                        Settings.playMode == PlayMode::Sequential ? "顺序" :
                            (Settings.playMode == PlayMode::RepeatOne ? "单曲" : "随机"),
                        String(Settings.brightness * 100 / 255) + "%", "ENTER", Web.address(),
                        Settings.apSsid, Settings.apPassword.length() ? "********" : "未设置", "ENTER"};
    int first = constrain(App.settingsCursor - 2, 0, 7);
    for (int row = 0; row < 3; ++row) {
        int index = first + row;
        int y = 35 + row * 18;
        bool selected = index == App.settingsCursor;
        canvas.fillRoundRect(7 + xOffset, y - 2, 226, 17, 5,
                             selected ? blend(card(), accent(), 0.18f) : card());
        text(names[index], 13 + xOffset, y - 1, 116, selected ? label() : secondary(), FontRole::Body);
        text(values[index], 225 + xOffset, y + 1, (index >= 6 && index <= 8) ? 92 : 51,
             selected ? accent() : secondary(), FontRole::Tiny, TR_DATUM);
        if (index == 4) {
            canvas.fillRoundRect(151 + xOffset, y + 5, 42, 3, 1, blend(card(), secondary(), 0.25f));
            canvas.fillRoundRect(151 + xOffset, y + 5, Settings.brightness * 42 / 255, 3, 1, accent());
        }
    }
    canvas.fillRoundRect(236 + xOffset, 35 + App.settingsCursor * 5, 2, 10, 1, secondary());
}

void LazerUI::drawMiniPlayer() {
    const Track* track = currentTrack();
    if (!track) return;
    uint16_t glass = blend(card(), background(), 0.18f);
    canvas.fillRoundRect(4, 96, 232, 20, 7, glass);
    canvas.drawRoundRect(4, 96, 232, 20, 7, blend(card(), secondary(), 0.24f));
    drawCover(7, 98, 16, track->seed, false);
    text(track->title, 28, 98, 142, label(), FontRole::Body);
    if (Audio.state() == AudioState::Loading) text("载入中", 154, 101, 32, accent(), FontRole::Tiny);
    drawPlayIcon(194, 102, Audio.playing(), label());
    drawNextIcon(218, 102, false, label());
}

void LazerUI::drawTabs() {
    const char* names[4] = {"主页", "音乐库", "我喜欢", "设置"};
    canvas.fillRect(0, 118, 240, 17, blend(card(), background(), 0.28f));
    line(0, 118, 240, blend(card(), secondary(), 0.20f));
    float tab = constrain(_tabMotion.value, 0.0f, 3.0f);
    canvas.fillRoundRect((int)(tab * 60.0f) + 18, 118, 24, 2, 1, accent());
    for (int i = 0; i < 4; ++i) {
        uint16_t tint = i == (uint8_t)App.screen ? accent() : secondary();
        drawTabIcon(i, i * 60 + 12, 124, tint);
        text(names[i], i * 60 + 22, 121, 36, tint, FontRole::Tiny);
    }
}

void LazerUI::drawNowPlaying(int yOffset) {
    const Track* track = currentTrack();
    uint32_t seed = track ? track->seed : 0x4c617a65;
    drawMesh(seed, yOffset);
    uint16_t white = Settings.dark ? color(255, 255, 255) : color(30, 25, 30);
    uint16_t dim = blend(white, background(), 0.35f);
    // Compact navigation chrome.
    canvas.drawLine(9, yOffset + 7, 13, yOffset + 11, white);
    canvas.drawLine(13, yOffset + 11, 17, yOffset + 7, white);
    text("正在播放", 120, yOffset + 2, 90, white, FontRole::Tiny, TC_DATUM);
    drawQueueIcon(220, yOffset + 5, white);

    if (track && App.lyricsMode) {
        drawCover(8, yOffset + 21, 56, seed, Audio.playing());
        text(track->title, 8, yOffset + 80, 65, white, FontRole::Tiny);
        drawLyrics(yOffset);
    } else if (track) {
        drawCover(88, yOffset + 16, 64, seed, Audio.playing());
        text(track->title, 120, yOffset + 83, 216, white, FontRole::Body, TC_DATUM);
    } else {
        text("没有正在播放的音乐", 120, yOffset + 51, 210, white, FontRole::Body, TC_DATUM);
    }
    drawTransport(yOffset);
}

void LazerUI::drawLyrics(int yOffset) {
    const Track* track = currentTrack();
    if (!track) return;
    if (millis() - _lastLyricSnapshot > 100 || _lyricView.trackSeed != track->seed) {
        Lyrics.snapshot(Audio.position() * 1000, _lyricView);
        _lastLyricSnapshot = millis();
    }
    const LyricView& view = _lyricView;
    if (!view.available || view.trackSeed != track->seed) {
        text("这首歌暂未提供歌词", 78, yOffset + 45, 154, secondary(), FontRole::Body);
        text("添加同名 .lrc 文件即可显示", 78, yOffset + 61, 154, secondary(), FontRole::Tiny);
        return;
    }
    int activeSlot = view.active;
    if (view.activeLine != _lyricIndex || view.trackSeed != _lyricSeed) {
        _lyricIndex = view.activeLine;
        _lyricSeed = view.trackSeed;
        _lyricMotion.snap(0);
        _lyricMotion.retarget(1, 0.40f, 0.0f, Settings.reduceMotion);
    }
    uint16_t bright = color(255, 255, 255);
    uint16_t dim = Settings.dark ? color(130, 125, 135) : color(85, 75, 90);
    float travel = (1.0f - _lyricMotion.value) * 11.0f;
    for (size_t slot = 0; slot < LyricView::VISIBLE; ++slot) {
        const auto& lyric = view.lines[slot];
        if (!lyric.valid || lyric.relative < -2 || lyric.relative > 2) continue;
        int y = yOffset + 26 + (lyric.relative + 2) * 13 + (int)travel;
        bool active = (int)slot == activeSlot;
        if (!active) {
            text(lyric.text, 79, y, 153, dim, FontRole::Tiny);
            continue;
        }
        setFont(FontRole::Body, bright);
        canvas.setTextDatum(TL_DATUM);
        int width = min(153, (int)canvas.textWidth(lyric.text));
        canvas.setClipRect(79, y, 153, 16);
        canvas.drawString(lyric.text, 79, y);
        canvas.clearClipRect();
        uint32_t elapsed = Audio.position() * 1000;
        float progress = 0.0f;
        if (lyric.endMs <= lyric.startMs) progress = 1.0f;
        else if (elapsed > lyric.startMs) {
            progress = (float)min(elapsed - lyric.startMs, lyric.endMs - lyric.startMs) /
                       (lyric.endMs - lyric.startMs);
        }
        uint16_t accentSoft = blend(dim, accent(), 0.65f);
        canvas.setClipRect(79, y, 153, 16);
        canvas.setTextColor(accentSoft);
        canvas.drawString(lyric.text, 79, y);
        canvas.clearClipRect();
        canvas.setClipRect(79, y, max(1, (int)(width * progress)), 16);
        canvas.setTextColor(bright);
        canvas.drawString(lyric.text, 79, y);
        canvas.clearClipRect();
    }
}

void LazerUI::drawTransport(int yOffset) {
    const Track* track = currentTrack();
    uint16_t shelf = Settings.dark ? color(18, 16, 20) : color(244, 240, 245);
    canvas.fillRect(0, yOffset + 99, 240, 36, shelf);
    uint32_t position = Audio.position();
    uint32_t duration = Audio.duration();
    canvas.fillRoundRect(9, yOffset + 101, 222, 2, 1, blend(shelf, secondary(), 0.35f));
    if (duration) canvas.fillRoundRect(9, yOffset + 101, max(1, (int)(position * 222 / duration)), 2, 1, accent());
    text(timeString(position), 9, yOffset + 105, 30, secondary(), FontRole::Tiny);
    text(timeString(duration), 230, yOffset + 105, 30, secondary(), FontRole::Tiny, TR_DATUM);
    bool favorite = track && Settings.isFavorite(track->seed);
    drawHeart(15, yOffset + 119, favorite, favorite ? accent() : secondary());
    drawNextIcon(62, yOffset + 118, true, label());
    canvas.fillCircle(119, yOffset + 121, 13, label());
    drawPlayIcon(114, yOffset + 115, Audio.playing(), background(), 1);
    drawNextIcon(164, yOffset + 118, false, label());
    drawQueueIcon(216, yOffset + 116, App.queueOpen ? accent() : secondary());
    // Thin volume indicator doubles as immediate feedback for +/- keys.
    canvas.fillRoundRect(190, yOffset + 131, 38, 2, 1, blend(shelf, secondary(), 0.30f));
    canvas.fillRoundRect(190, yOffset + 131, Settings.volume * 38 / 20, 2, 1, accent());
}

void LazerUI::drawQueue() {
    uint16_t panel = Settings.dark ? color(26, 25, 29) : color(250, 249, 252);
    canvas.fillRoundRect(12, 6, 216, 123, 12, panel);
    canvas.drawRoundRect(12, 6, 216, 123, 12, blend(panel, secondary(), 0.35f));
    text("播放队列", 22, 11, 120, label(), FontRole::Title);
    text("Q / ESC 关闭", 216, 15, 78, secondary(), FontRole::Tiny, TR_DATUM);
    int count = App.listCount();
    if (!count) {
        text("队列为空", 24, 52, 180, secondary(), FontRole::Body);
        return;
    }
    auto favorites = App.screen == Screen::Favorites ? App.favoriteIndices() : std::vector<int>();
    int first = constrain(App.selected - 2, 0, max(0, count - 5));
    for (int row = 0; row < 5 && first + row < count; ++row) {
        int listIndex = first + row;
        int trackIndex = favorites.size() ? favorites[listIndex] : listIndex;
        const Track* track = Library.track(trackIndex);
        int y = 38 + row * 17;
        bool selected = listIndex == App.selected;
        bool current = trackIndex == Audio.currentTrack();
        if (selected) canvas.fillRoundRect(18, y - 2, 204, 16, 5, blend(panel, accent(), 0.20f));
        text(String(listIndex + 1), 23, y, 18, current ? accent() : secondary(), FontRole::Tiny);
        text(track->title, 43, y - 1, 133, current ? accent() : label(), FontRole::Body);
        text(timeString(track->duration), 211, y + 1, 32, secondary(), FontRole::Tiny, TR_DATUM);
    }
}

void LazerUI::drawHelp() {
    uint16_t panel = Settings.dark ? color(22, 22, 25) : color(252, 252, 253);
    canvas.fillRoundRect(6, 4, 228, 127, 10, panel);
    canvas.drawRoundRect(6, 4, 228, 127, 10, accent());
    text("键盘控制", 15, 9, 120, label(), FontRole::Title);
    text("H / ESC 关闭", 222, 12, 90, secondary(), FontRole::Tiny, TR_DATUM);
    const char* left[] = {"SPACE  播放 / 暂停", "ENTER  播放所选", "M      全屏播放器", "L      歌词 / 封面", "Q      播放队列", "F      收藏歌曲"};
    const char* right[] = {"W/S 或方向键  选择", "A/D     快退 / 快进 5 秒", "N/P     下一首 / 上一首", "+/-     音量", "R       播放模式", "TAB / 1-4  切换页面"};
    for (int i = 0; i < 6; ++i) {
        text(left[i], 16, 35 + i * 14, 106, i == 0 ? accent() : label(), FontRole::Tiny);
        text(right[i], 126, 35 + i * 14, 103, label(), FontRole::Tiny);
    }
}

void LazerUI::drawTextEditor() {
    uint16_t panel = Settings.dark ? color(20, 18, 23) : color(252, 252, 253);
    canvas.fillRoundRect(5, 17, 230, 102, 10, panel);
    canvas.drawRoundRect(5, 17, 230, 102, 10, accent());
    text(App.textEditPasswordTarget ? "设置 AP 密码" : "设置 AP 名称", 15, 23, 160,
         label(), FontRole::Body);
    text("ESC 取消", 222, 26, 62, secondary(), FontRole::Tiny, TR_DATUM);
    canvas.fillRoundRect(12, 46, 216, 34, 5, blend(panel, background(), 0.45f));
    String visible = App.textEditSecret ? "********" : App.textEditValue;
    if (visible.length() > 28) {
        int start = visible.length() - 28;
        while (start < (int)visible.length() && ((uint8_t)visible[start] & 0xc0) == 0x80) ++start;
        visible = visible.substring(start);
    }
    text(visible.length() ? visible : "请输入…", 18, 52, 202,
         visible.length() ? label() : secondary(), FontRole::Tiny);
    setFont(FontRole::Tiny, accent());
    int cursorX = 18 + min(198, (int)canvas.textWidth(visible) + 2);
    if ((millis() / 450) % 2 == 0) canvas.fillRect(cursorX, 52, 1, 12, accent());
    text(App.textEditPasswordTarget ? "8–63 位；区分大小写" : "1–32 字节；支持大小写",
         16, 88, 150, secondary(), FontRole::Tiny);
    text("ENTER 保存", 222, 99, 76, accent(), FontRole::Tiny, TR_DATUM);
}

void LazerUI::drawToast() {
    setFont(FontRole::Body, color(255, 255, 255));
    int width = min(216, max(70, (int)canvas.textWidth(App.toast) + 22));
    int x = (240 - width) / 2;
    canvas.fillRoundRect(x, 105, width, 22, 9, color(38, 35, 40));
    text(App.toast, 120, 109, width - 12, color(255, 255, 255), FontRole::Body, TC_DATUM);
}

void LazerUI::drawPlayIcon(int x, int y, bool playing, uint16_t tint, int scale) {
    if (playing) {
        canvas.fillRect(x, y, 3 * scale, 10 * scale, tint);
        canvas.fillRect(x + 6 * scale, y, 3 * scale, 10 * scale, tint);
    } else {
        canvas.fillTriangle(x, y, x, y + 10 * scale, x + 8 * scale, y + 5 * scale, tint);
    }
}

void LazerUI::drawNextIcon(int x, int y, bool previous, uint16_t tint) {
    if (previous) {
        canvas.fillRect(x, y, 2, 10, tint);
        canvas.fillTriangle(x + 10, y, x + 10, y + 10, x + 2, y + 5, tint);
        canvas.fillTriangle(x + 16, y, x + 16, y + 10, x + 8, y + 5, tint);
    } else {
        canvas.fillTriangle(x, y, x, y + 10, x + 8, y + 5, tint);
        canvas.fillTriangle(x + 6, y, x + 6, y + 10, x + 14, y + 5, tint);
        canvas.fillRect(x + 15, y, 2, 10, tint);
    }
}

void LazerUI::drawHeart(int x, int y, bool filled, uint16_t tint) {
    if (filled) {
        canvas.fillCircle(x + 3, y + 3, 3, tint);
        canvas.fillCircle(x + 8, y + 3, 3, tint);
        canvas.fillTriangle(x, y + 4, x + 11, y + 4, x + 5, y + 11, tint);
    } else {
        canvas.drawCircle(x + 3, y + 3, 3, tint);
        canvas.drawCircle(x + 8, y + 3, 3, tint);
        canvas.drawLine(x, y + 4, x + 5, y + 11, tint);
        canvas.drawLine(x + 11, y + 4, x + 5, y + 11, tint);
    }
}

void LazerUI::drawQueueIcon(int x, int y, uint16_t tint) {
    canvas.fillTriangle(x, y + 2, x, y + 8, x + 5, y + 5, tint);
    canvas.fillRect(x + 8, y + 1, 10, 2, tint);
    canvas.fillRect(x + 8, y + 5, 10, 2, tint);
    canvas.fillRect(x + 8, y + 9, 10, 2, tint);
}

void LazerUI::drawTabIcon(int tab, int x, int y, uint16_t tint) {
    switch (tab) {
        case 0:
            canvas.drawLine(x, y + 3, x + 4, y - 1, tint);
            canvas.drawLine(x + 4, y - 1, x + 8, y + 3, tint);
            canvas.drawRect(x + 1, y + 3, 7, 7, tint);
            break;
        case 1:
            canvas.drawRect(x, y, 8, 8, tint);
            canvas.drawLine(x + 2, y + 2, x + 6, y + 2, tint);
            canvas.drawLine(x + 2, y + 5, x + 6, y + 5, tint);
            break;
        case 2:
            drawHeart(x - 1, y - 1, true, tint);
            break;
        default:
            canvas.drawCircle(x + 4, y + 4, 4, tint);
            canvas.fillCircle(x + 4, y + 4, 1, tint);
            canvas.drawFastHLine(x + 3, y - 2, 3, tint);
            canvas.drawFastHLine(x + 3, y + 10, 3, tint);
            break;
    }
}
