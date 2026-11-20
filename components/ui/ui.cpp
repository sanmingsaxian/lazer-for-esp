#include "ui.h"
#include "app.h"
#include "audio_engine.h"
#include "board.h"
#include "library.h"
#include "lyrics.h"
#include "netease.h"
#include "settings.h"
#include "strutil.h"
#include "web.h"

#include <lvgl.h>
#include <qrcodegen.h>

#include <cmath>
#include <cstdio>
#include <cstring>

LV_FONT_DECLARE(font_cn_16);
LV_FONT_DECLARE(font_cn_10);

namespace lz {

namespace {

// ---------------------------------------------------------------- palette
constexpr uint32_t kMusicRed = 0xfa2d48;

lv_color_t rgb(uint8_t r, uint8_t g, uint8_t b) { return lv_color_make(r, g, b); }

lv_color_t blend(lv_color_t a, lv_color_t b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return lv_color_make((uint8_t)(a.red + (b.red - a.red) * t),
                         (uint8_t)(a.green + (b.green - a.green) * t),
                         (uint8_t)(a.blue + (b.blue - a.blue) * t));
}

lv_color_t hsvColor(float h, float s, float v) {
    h = h - floorf(h);
    float r, g, b;
    const int i = (int)floorf(h * 6.0f);
    const float f = h * 6.0f - i;
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return rgb((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
}

void palette(uint32_t seed, lv_color_t* colors) {
    const float h = (seed & 0xff) / 255.0f;
    colors[0] = hsvColor(h, 0.72f, 0.84f);
    colors[1] = hsvColor(h + 0.09f, 0.64f, 0.68f);
    colors[2] = hsvColor(h + 0.52f, 0.58f, 0.58f);
}

lv_color_t background() { return Settings.dark ? rgb(0, 0, 0) : rgb(242, 242, 247); }
lv_color_t card() { return Settings.dark ? rgb(28, 28, 30) : rgb(255, 255, 255); }
lv_color_t labelColor() { return Settings.dark ? rgb(255, 255, 255) : rgb(0, 0, 0); }
lv_color_t secondary() { return Settings.dark ? rgb(158, 158, 165) : rgb(105, 105, 115); }
lv_color_t accent() { return rgb(250, 45, 72); }

// ---------------------------------------------------------------- helpers
lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* roundedBox(lv_obj_t* parent, int x, int y, int w, int h, int radius, lv_color_t color) {
    lv_obj_t* obj = box(parent, x, y, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    lv_obj_t* obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_label_set_text(obj, text);
    return obj;
}

void place(lv_obj_t* obj, int x, int y) { lv_obj_set_pos(obj, x, y); }

void setText(lv_obj_t* obj, const std::string& text) {
    if (strcmp(lv_label_get_text(obj), text.c_str()) != 0) lv_label_set_text(obj, text.c_str());
}

// Constrain a label to a fixed width with ellipsis, matching the original
// clip-rect text behaviour.
void clipped(lv_obj_t* obj, int width) {
    lv_obj_set_width(obj, width);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
}

void recolor(lv_obj_t* obj, lv_color_t color) { lv_obj_set_style_text_color(obj, color, 0); }

void fill(lv_obj_t* obj, lv_color_t color) { lv_obj_set_style_bg_color(obj, color, 0); }

std::string timeString(uint32_t seconds) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%lu:%02lu", (unsigned long)(seconds / 60),
             (unsigned long)(seconds % 60));
    return buffer;
}

const Track* currentTrack() {
    return Audio.currentTrack() >= 0 ? Library.track(Audio.currentTrack()) : nullptr;
}

uint32_t currentSeed() {
    const Track* track = currentTrack();
    if (track) return track->seed;
    if (Audio.onlineActive()) return Audio.onlineSeed();
    return 0x4c617a65;
}

std::string currentTitle() {
    const Track* track = currentTrack();
    if (track) return track->title;
    if (Audio.onlineActive()) return Audio.onlineTitle();
    return "";
}

// ---------------------------------------------------------------- cover art
void drawCover(lv_obj_t* canvas, int size, uint32_t seed, bool playing) {
    lv_color_t p[3];
    palette(seed, p);
    lv_canvas_fill_bg(canvas, p[0], LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_rect_dsc_t dsc;
    lv_area_t area;
    for (int i = 0; i < size; i += 4) {
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = blend(p[0], p[1], (float)i / size * 0.75f);
        dsc.bg_opa = LV_OPA_COVER;
        area = {2, i, size - 3, i + (size - i < 4 ? size - i : 4) - 1};
        lv_draw_rect(&layer, &dsc, &area);
    }
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.bg_color = blend(p[1], p[2], 0.45f);
    const int c1x = (int)(size * 0.78f), c1y = (int)(size * 0.18f), r1 = (int)(size * 0.36f);
    area = {c1x - r1, c1y - r1, c1x + r1, c1y + r1};
    lv_draw_rect(&layer, &dsc, &area);
    dsc.bg_color = blend(p[0], p[2], 0.55f);
    const int c2x = (int)(size * 0.12f), c2y = (int)(size * 0.95f), r2 = (int)(size * 0.52f);
    area = {c2x - r2, c2y - r2, c2x + r2, c2y + r2};
    lv_draw_rect(&layer, &dsc, &area);
    // Music note glyph, same construction as the original vector cover.
    const lv_color_t white = rgb(255, 245, 248);
    const int cx = size / 2, cy = size / 2;
    const int unit = size / 24 > 0 ? size / 24 : 1;
    dsc.radius = 0;
    dsc.bg_color = white;
    area = {cx, cy - size / 4, cx + unit * 2 - 1, cy + size / 4 - 1};
    lv_draw_rect(&layer, &dsc, &area);
    area = {cx, cy - size / 4, cx + size / 5 - 1, cy - size / 4 + unit * 3 - 1};
    lv_draw_rect(&layer, &dsc, &area);
    dsc.radius = LV_RADIUS_CIRCLE;
    const int nr = size / 8;
    area = {cx - size / 8 - nr, cy + size / 5 - nr, cx - size / 8 + nr, cy + size / 5 + nr};
    lv_draw_rect(&layer, &dsc, &area);
    if (playing && size >= 40) {
        dsc.radius = 0;
        for (int i = 0; i < 3; ++i) {
            const int h = 3 + ((millis() / 180 + i * 2) % 7);
            area = {size - 15 + i * 4, size - 5 - h, size - 15 + i * 4 + 1, size - 6};
            lv_draw_rect(&layer, &dsc, &area);
        }
    }
    lv_canvas_finish_layer(canvas, &layer);
}

lv_obj_t* makeCover(lv_obj_t* parent, int x, int y, int size, uint8_t* buf) {
    lv_obj_t* canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, buf, size, size, LV_COLOR_FORMAT_RGB565);
    place(canvas, x, y);
    return canvas;
}

// ---------------------------------------------------------------- widgets
struct Ui {
    // browse
    lv_obj_t* browse;
    lv_obj_t* statusName;
    lv_obj_t* batteryFrame;
    lv_obj_t* batteryTip;
    lv_obj_t* batteryFill;
    lv_obj_t* pages[4];
    // home
    lv_obj_t* homeHero;
    lv_obj_t* homeCover;
    lv_obj_t* homeKicker;
    lv_obj_t* homeTitle;
    lv_obj_t* homePlay;
    lv_obj_t* homeEmpty;
    lv_obj_t* quickRow[2];
    lv_obj_t* quickDot[2];
    lv_obj_t* quickTitle[2];
    lv_obj_t* quickHeart[2];
    // library / favorites (independent widget sets per page)
    lv_obj_t* libTitle[2];
    lv_obj_t* libCount[2];
    lv_obj_t* libSource;
    lv_obj_t* libSourceName;
    lv_obj_t* libSourceState[2];
    lv_obj_t* libEmpty[2];
    lv_obj_t* libRow[2][3];
    lv_obj_t* libIcon[2][3];
    lv_obj_t* libRowTitle[2][3];
    lv_obj_t* libHeart[2][3];
    lv_obj_t* libDuration[2][3];
    lv_obj_t* libScroll[2];
    // settings
    lv_obj_t* setRow[3];
    lv_obj_t* setName[3];
    lv_obj_t* setValue[3];
    lv_obj_t* setBrightBar;
    lv_obj_t* setBrightFill;
    lv_obj_t* setScroll;
    // mini player + tabs
    lv_obj_t* mini;
    lv_obj_t* miniCover;
    lv_obj_t* miniTitle;
    lv_obj_t* miniLoading;
    lv_obj_t* miniPlay;
    lv_obj_t* miniNext;
    lv_obj_t* tabBar;
    lv_obj_t* tabIndicator;
    lv_obj_t* tabLabel[4];
    // player
    lv_obj_t* player;
    lv_obj_t* mesh[30];
    lv_obj_t* playerHeader;
    lv_obj_t* playerTitle;
    lv_obj_t* playerQueueIcon;
    lv_obj_t* playerCoverSmall;
    lv_obj_t* playerCoverBig;
    lv_obj_t* playerSongTitle;
    lv_obj_t* playerSongTitleBig;
    lv_obj_t* playerEmpty;
    lv_obj_t* lyricLine[5];
    lv_obj_t* lyricClip;
    lv_obj_t* lyricBright;
    lv_obj_t* lyricNone;
    lv_obj_t* lyricHint;
    lv_obj_t* shelf;
    lv_obj_t* progressBg;
    lv_obj_t* progressFill;
    lv_obj_t* timePos;
    lv_obj_t* timeDur;
    lv_obj_t* playerHeart;
    lv_obj_t* playerPrev;
    lv_obj_t* playCircle;
    lv_obj_t* playerPlay;
    lv_obj_t* playerNext;
    lv_obj_t* playerQueue;
    lv_obj_t* volumeBg;
    lv_obj_t* volumeFill;
    // overlays
    lv_obj_t* queuePanel;
    lv_obj_t* queueEmpty;
    lv_obj_t* queueNum[5];
    lv_obj_t* queueTitle[5];
    lv_obj_t* queueDuration[5];
    lv_obj_t* queueRow[5];
    lv_obj_t* helpPanel;
    lv_obj_t* neteasePanel;
    lv_obj_t* neteaseTitle;
    lv_obj_t* neteaseMessage;
    lv_obj_t* neteaseRow[6];
    lv_obj_t* neteaseRowName[6];
    lv_obj_t* neteaseRowValue[6];
    lv_obj_t* neteaseHint;
    lv_obj_t* qrCanvas;
    lv_obj_t* qrPlaceholder;
    lv_obj_t* qrStatus;
    lv_obj_t* qrHint1;
    lv_obj_t* qrHint2;
    lv_obj_t* qrWarn1;
    lv_obj_t* qrWarn2;
    lv_obj_t* editorPanel;
    lv_obj_t* editorTitle;
    lv_obj_t* editorBox;
    lv_obj_t* editorValue;
    lv_obj_t* editorCursor;
    lv_obj_t* editorHint;
    lv_obj_t* toast;
    lv_obj_t* toastLabel;

    // state
    uint32_t lastFrame = 0;
    bool lastPlayerOpen = false;
    Screen lastScreen = Screen::Home;
    bool lastDark = true;
    uint32_t coverSeed[4] = {0, 0, 0, 0};
    int coverPlayingTick[4] = {-1, -1, -1, -1};
    uint32_t lastLyricSnapshot = 0;
    LyricSnapshot lyricSnapshot;
    int lyricIndex = -1;
    uint32_t lyricSeed = 0;
    uint32_t lyricChangeAt = 0;
    std::string lastQrUrl;
    bool playerAnimRunning = false;
};

Ui ui;
uint8_t coverBufHome[26 * 26 * 2];
uint8_t coverBufMini[16 * 16 * 2];
uint8_t coverBufSmall[56 * 56 * 2];
uint8_t coverBufBig[64 * 64 * 2];
uint8_t qrBuf[98 * 98 * 2];

// ---------------------------------------------------------------- animations
void playerAnimCb(void* var, int32_t value) {
    lv_obj_set_y(static_cast<lv_obj_t*>(var), value);
}

void animatePlayer(bool open) {
    const int from = open ? 135 : 0;
    const int to = open ? 0 : 135;
    lv_obj_set_y(ui.player, from);
    if (Settings.reduceMotion) {
        lv_obj_set_y(ui.player, to);
        return;
    }
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, ui.player);
    lv_anim_set_exec_cb(&anim, playerAnimCb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_duration(&anim, 460);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);
}

void animateTab(int screen) {
    const int to = screen * 60 + 18;
    if (Settings.reduceMotion) {
        lv_obj_set_x(ui.tabIndicator, to);
        return;
    }
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, ui.tabIndicator);
    lv_anim_set_exec_cb(&anim, [](void* var, int32_t v) {
        lv_obj_set_x(static_cast<lv_obj_t*>(var), v);
    });
    lv_anim_set_values(&anim, lv_obj_get_x(ui.tabIndicator), to);
    lv_anim_set_duration(&anim, 350);
    lv_anim_set_path_cb(&anim, lv_anim_path_overshoot);
    lv_anim_start(&anim);
}

// ---------------------------------------------------------------- build
void buildBrowse() {
    ui.browse = box(lv_screen_active(), 0, 0, 240, 135);
    ui.statusName = label(ui.browse, "Lazer", &font_cn_10, secondary());
    place(ui.statusName, 7, 1);
    ui.batteryFrame = box(ui.browse, 216, 3, 16, 7);
    lv_obj_set_style_border_color(ui.batteryFrame, secondary(), 0);
    lv_obj_set_style_border_width(ui.batteryFrame, 1, 0);
    ui.batteryTip = roundedBox(ui.browse, 232, 5, 2, 3, 0, secondary());
    ui.batteryFill = roundedBox(ui.browse, 218, 5, 12, 3, 0, secondary());

    for (int i = 0; i < 4; ++i) ui.pages[i] = box(ui.browse, 0, 0, 240, 135);

    // Home
    lv_obj_t* home = ui.pages[0];
    lv_obj_t* t = label(home, "现在就听", &font_cn_16, labelColor());
    place(t, 8, 13);
    ui.homeHero = roundedBox(home, 7, 34, 226, 34, 9, card());
    ui.homeCover = makeCover(home, 11, 38, 26, coverBufHome);
    ui.homeKicker = label(home, "", &font_cn_10, accent());
    place(ui.homeKicker, 43, 37);
    ui.homeTitle = label(home, "", &font_cn_16, labelColor());
    place(ui.homeTitle, 43, 49);
    clipped(ui.homeTitle, 145);
    ui.homePlay = label(home, ">", &font_cn_16, accent());
    place(ui.homePlay, 213, 45);
    ui.homeEmpty = label(home, "放入本地 MP3，或在 WebUI 播放网易云", &font_cn_16, secondary());
    place(ui.homeEmpty, 15, 43);
    t = label(home, "快速选取", &font_cn_10, secondary());
    place(t, 9, 70);
    for (int r = 0; r < 2; ++r) {
        ui.quickRow[r] = roundedBox(home, 7, 82 + r * 15, 226, 14, 4, card());
        ui.quickDot[r] = roundedBox(home, 12, 86 + r * 15, 6, 6, 3, secondary());
        ui.quickTitle[r] = label(home, "", &font_cn_10, secondary());
        place(ui.quickTitle[r], 24, 82 + r * 15);
        clipped(ui.quickTitle[r], 165);
        ui.quickHeart[r] = label(home, "♥", &font_cn_10, accent());
        place(ui.quickHeart[r], 220, 86 + r * 15);
    }

    // Library / Favorites: independent widget sets per page.
    for (int page = 1; page <= 2; ++page) {
        lv_obj_t* pg = ui.pages[page];
        const int idx = page - 1;
        const bool favorites = page == 2;
        ui.libTitle[idx] = label(pg, favorites ? "我喜欢" : "音乐库", &font_cn_16, labelColor());
        place(ui.libTitle[idx], 8, 13);
        ui.libCount[idx] = label(pg, "", &font_cn_10, secondary());
        place(ui.libCount[idx], 191, 18);
        if (!favorites) {
            ui.libSource = roundedBox(pg, 7, 34, 226, 18, 6, card());
            roundedBox(pg, 11, 38, 10, 10, 3, accent());
            ui.libSourceName = label(pg, "本地音乐", &font_cn_16, labelColor());
            place(ui.libSourceName, 28, 35);
        }
        ui.libSourceState[idx] = label(pg, favorites ? "" : "SD 卡", &font_cn_10, secondary());
        place(ui.libSourceState[idx], 181, 38);
        ui.libEmpty[idx] = label(pg, favorites ? "按 F 收藏喜欢的歌曲" : "没有找到 MP3 文件",
                                 &font_cn_16, secondary());
        place(ui.libEmpty[idx], 12, favorites ? 49 : 61);
        const int startY = favorites ? 35 : 55;
        const int rows = favorites ? 3 : 2;
        for (int r = 0; r < 3; ++r) {
            const int y = startY + r * 17;
            ui.libRow[idx][r] = roundedBox(pg, 6, y - 2, 228, 16, 5, card());
            ui.libIcon[idx][r] = roundedBox(pg, 10, y, 12, 12, 3, card());
            ui.libRowTitle[idx][r] = label(pg, "", &font_cn_16, secondary());
            place(ui.libRowTitle[idx][r], 28, y - 1);
            clipped(ui.libRowTitle[idx][r], 147);
            ui.libHeart[idx][r] = label(pg, "♥", &font_cn_10, accent());
            place(ui.libHeart[idx][r], 188, y + 2);
            ui.libDuration[idx][r] = label(pg, "", &font_cn_10, secondary());
            place(ui.libDuration[idx][r], 207, y + 1);
            if (r >= rows) {
                lv_obj_add_flag(ui.libRow[idx][r], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui.libIcon[idx][r], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui.libRowTitle[idx][r], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui.libHeart[idx][r], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui.libDuration[idx][r], LV_OBJ_FLAG_HIDDEN);
            }
        }
        ui.libScroll[idx] = roundedBox(pg, 236, startY, 2, 12, 1, secondary());
    }

    // Settings
    lv_obj_t* set = ui.pages[3];
    t = label(set, "设置", &font_cn_16, labelColor());
    place(t, 8, 13);
    for (int r = 0; r < 3; ++r) {
        const int y = 35 + r * 18;
        ui.setRow[r] = roundedBox(set, 7, y - 2, 226, 17, 5, card());
        ui.setName[r] = label(set, "", &font_cn_16, secondary());
        place(ui.setName[r], 13, y - 1);
        ui.setValue[r] = label(set, "", &font_cn_10, secondary());
        place(ui.setValue[r], 130, y + 1);
    }
    ui.setBrightBar = roundedBox(set, 151, 40, 42, 3, 1, secondary());
    ui.setBrightFill = roundedBox(set, 151, 40, 20, 3, 1, accent());
    ui.setScroll = roundedBox(set, 236, 35, 2, 10, 1, secondary());

    // Mini player
    ui.mini = roundedBox(ui.browse, 4, 96, 232, 20, 7, card());
    lv_obj_set_style_border_color(ui.mini, secondary(), 0);
    lv_obj_set_style_border_width(ui.mini, 1, 0);
    ui.miniCover = makeCover(ui.browse, 7, 98, 16, coverBufMini);
    ui.miniTitle = label(ui.browse, "", &font_cn_16, labelColor());
    place(ui.miniTitle, 28, 98);
    clipped(ui.miniTitle, 142);
    ui.miniLoading = label(ui.browse, "载入中", &font_cn_10, accent());
    place(ui.miniLoading, 154, 101);
    ui.miniPlay = label(ui.browse, ">", &font_cn_16, labelColor());
    place(ui.miniPlay, 194, 100);
    ui.miniNext = label(ui.browse, ">|", &font_cn_10, labelColor());
    place(ui.miniNext, 218, 102);

    // Tab bar
    ui.tabBar = box(ui.browse, 0, 118, 240, 17);
    ui.tabIndicator = roundedBox(ui.browse, 18, 118, 24, 2, 1, accent());
    const char* names[4] = {"主页", "音乐库", "我喜欢", "设置"};
    for (int i = 0; i < 4; ++i) {
        ui.tabLabel[i] = label(ui.browse, names[i], &font_cn_10, secondary());
        place(ui.tabLabel[i], i * 60 + 22, 121);
    }
}

void buildPlayer() {
    ui.player = box(lv_screen_active(), 0, 135, 240, 135);
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 6; ++col)
            ui.mesh[row * 6 + col] = roundedBox(ui.player, col * 40, row * 20, 40, 20, 0,
                                                rgb(8, 7, 10));
    ui.playerHeader = label(ui.player, "v", &font_cn_10, rgb(255, 255, 255));
    place(ui.playerHeader, 9, 2);
    ui.playerTitle = label(ui.player, "正在播放", &font_cn_10, rgb(255, 255, 255));
    place(ui.playerTitle, 90, 2);
    ui.playerQueueIcon = label(ui.player, "≡", &font_cn_16, rgb(255, 255, 255));
    place(ui.playerQueueIcon, 220, 1);

    ui.playerCoverSmall = makeCover(ui.player, 8, 21, 56, coverBufSmall);
    ui.playerCoverBig = makeCover(ui.player, 88, 16, 64, coverBufBig);
    ui.playerSongTitle = label(ui.player, "", &font_cn_10, rgb(255, 255, 255));
    place(ui.playerSongTitle, 8, 80);
    clipped(ui.playerSongTitle, 65);
    ui.playerSongTitleBig = label(ui.player, "", &font_cn_16, rgb(255, 255, 255));
    place(ui.playerSongTitleBig, 12, 83);
    clipped(ui.playerSongTitleBig, 216);
    lv_obj_set_style_text_align(ui.playerSongTitleBig, LV_TEXT_ALIGN_CENTER, 0);
    ui.playerEmpty = label(ui.player, "没有正在播放的音乐", &font_cn_16, rgb(255, 255, 255));
    place(ui.playerEmpty, 40, 51);

    for (int i = 0; i < 5; ++i) {
        ui.lyricLine[i] = label(ui.player, "", &font_cn_10, rgb(130, 125, 135));
        place(ui.lyricLine[i], 79, 26 + i * 13);
        clipped(ui.lyricLine[i], 153);
    }
    ui.lyricClip = box(ui.player, 79, 26, 0, 16);
    lv_obj_set_style_clip_corner(ui.lyricClip, false, 0);
    ui.lyricBright = label(ui.lyricClip, "", &font_cn_16, rgb(255, 255, 255));
    place(ui.lyricBright, 0, 0);
    ui.lyricNone = label(ui.player, "这首歌暂未提供歌词", &font_cn_16, secondary());
    place(ui.lyricNone, 78, 45);
    ui.lyricHint = label(ui.player, "添加同名 .lrc 文件即可显示", &font_cn_10, secondary());
    place(ui.lyricHint, 78, 61);

    ui.shelf = box(ui.player, 0, 99, 240, 36);
    ui.progressBg = roundedBox(ui.player, 9, 101, 222, 2, 1, secondary());
    ui.progressFill = roundedBox(ui.player, 9, 101, 1, 2, 1, accent());
    ui.timePos = label(ui.player, "0:00", &font_cn_10, secondary());
    place(ui.timePos, 9, 105);
    ui.timeDur = label(ui.player, "0:00", &font_cn_10, secondary());
    place(ui.timeDur, 200, 105);
    ui.playerHeart = label(ui.player, "♡", &font_cn_16, secondary());
    place(ui.playerHeart, 12, 116);
    ui.playerPrev = label(ui.player, "|<", &font_cn_16, labelColor());
    place(ui.playerPrev, 58, 116);
    ui.playCircle = roundedBox(ui.player, 106, 108, 26, 26, 13, labelColor());
    ui.playerPlay = label(ui.playCircle, ">", &font_cn_16, background());
    lv_obj_center(ui.playerPlay);
    ui.playerNext = label(ui.player, ">|", &font_cn_16, labelColor());
    place(ui.playerNext, 162, 116);
    ui.playerQueue = label(ui.player, "≡", &font_cn_16, secondary());
    place(ui.playerQueue, 216, 114);
    ui.volumeBg = roundedBox(ui.player, 190, 131, 38, 2, 1, secondary());
    ui.volumeFill = roundedBox(ui.player, 190, 131, 20, 2, 1, accent());
}

void buildOverlays() {
    // Queue
    ui.queuePanel = roundedBox(lv_screen_active(), 12, 6, 216, 123, 12, rgb(26, 25, 29));
    lv_obj_set_style_border_color(ui.queuePanel, secondary(), 0);
    lv_obj_set_style_border_width(ui.queuePanel, 1, 0);
    lv_obj_t* t = label(ui.queuePanel, "播放队列", &font_cn_16, labelColor());
    place(t, 10, 5);
    t = label(ui.queuePanel, "Q / ESC 关闭", &font_cn_10, secondary());
    place(t, 138, 9);
    ui.queueEmpty = label(ui.queuePanel, "队列为空", &font_cn_16, secondary());
    place(ui.queueEmpty, 12, 46);
    for (int r = 0; r < 5; ++r) {
        const int y = 32 + r * 17;
        ui.queueRow[r] = roundedBox(ui.queuePanel, 6, y - 2, 204, 16, 5, rgb(26, 25, 29));
        ui.queueNum[r] = label(ui.queuePanel, "", &font_cn_10, secondary());
        place(ui.queueNum[r], 11, y);
        ui.queueTitle[r] = label(ui.queuePanel, "", &font_cn_16, labelColor());
        place(ui.queueTitle[r], 31, y - 1);
        clipped(ui.queueTitle[r], 133);
        ui.queueDuration[r] = label(ui.queuePanel, "", &font_cn_10, secondary());
        place(ui.queueDuration[r], 175, y + 1);
    }

    // Help
    ui.helpPanel = roundedBox(lv_screen_active(), 6, 4, 228, 127, 10, rgb(22, 22, 25));
    lv_obj_set_style_border_color(ui.helpPanel, accent(), 0);
    lv_obj_set_style_border_width(ui.helpPanel, 1, 0);
    t = label(ui.helpPanel, "键盘控制", &font_cn_16, labelColor());
    place(t, 9, 5);
    t = label(ui.helpPanel, "H / ESC 关闭", &font_cn_10, secondary());
    place(t, 140, 8);
    const char* left[] = {"SPACE  播放 / 暂停", "ENTER  播放所选", "M      全屏播放器",
                          "L      歌词 / 封面", "Q      播放队列", "F      收藏歌曲"};
    const char* right[] = {"W/S 或方向键  选择", "A/D     快退 / 快进 5 秒", "N/P     下一首 / 上一首",
                           "+/-     音量", "R       播放模式", "TAB / 1-4  切换页面"};
    for (int i = 0; i < 6; ++i) {
        lv_obj_t* l = label(ui.helpPanel, left[i], &font_cn_10, i == 0 ? accent() : labelColor());
        place(l, 10, 31 + i * 14);
        lv_obj_t* r = label(ui.helpPanel, right[i], &font_cn_10, labelColor());
        place(r, 120, 31 + i * 14);
    }

    // Netease panel
    ui.neteasePanel = roundedBox(lv_screen_active(), 4, 3, 232, 129, 10, rgb(18, 18, 22));
    lv_obj_set_style_border_color(ui.neteasePanel, accent(), 0);
    lv_obj_set_style_border_width(ui.neteasePanel, 1, 0);
    ui.neteaseTitle = label(ui.neteasePanel, "网易云账号", &font_cn_16, labelColor());
    place(ui.neteaseTitle, 9, 6);
    t = label(ui.neteasePanel, "ESC 关闭", &font_cn_10, secondary());
    place(t, 168, 8);
    ui.neteaseMessage = label(ui.neteasePanel, "", &font_cn_10, secondary());
    place(ui.neteaseMessage, 10, 26);
    clipped(ui.neteaseMessage, 212);
    for (int i = 0; i < 6; ++i) {
        const int y = 41 + i * 12;
        ui.neteaseRow[i] = roundedBox(ui.neteasePanel, 8, y - 2, 216, 12, 4, rgb(18, 18, 22));
        ui.neteaseRowName[i] = label(ui.neteasePanel, "", &font_cn_10, secondary());
        place(ui.neteaseRowName[i], 14, y);
        ui.neteaseRowValue[i] = label(ui.neteasePanel, "", &font_cn_10, secondary());
        place(ui.neteaseRowValue[i], 150, y);
        clipped(ui.neteaseRowValue[i], 72);
    }
    ui.neteaseHint = label(ui.neteasePanel, "公共 API 有账号风险，推荐自建", &font_cn_10, secondary());
    place(ui.neteaseHint, 10, 116);
    ui.qrCanvas = lv_canvas_create(ui.neteasePanel);
    lv_canvas_set_buffer(ui.qrCanvas, qrBuf, 98, 98, LV_COLOR_FORMAT_RGB565);
    place(ui.qrCanvas, 8, 28);
    ui.qrPlaceholder = roundedBox(ui.neteasePanel, 8, 28, 98, 98, 5, rgb(40, 40, 46));
    ui.qrStatus = label(ui.neteasePanel, "", &font_cn_16, labelColor());
    place(ui.qrStatus, 114, 32);
    clipped(ui.qrStatus, 114);
    ui.qrHint1 = label(ui.neteasePanel, "", &font_cn_10, secondary());
    place(ui.qrHint1, 114, 53);
    ui.qrHint2 = label(ui.neteasePanel, "", &font_cn_10, secondary());
    place(ui.qrHint2, 114, 67);
    ui.qrWarn1 = label(ui.neteasePanel, "公共 API 可接触登录请求", &font_cn_10, secondary());
    place(ui.qrWarn1, 114, 91);
    ui.qrWarn2 = label(ui.neteasePanel, "和 Cookie；建议自建服务", &font_cn_10, secondary());
    place(ui.qrWarn2, 114, 104);

    // Text editor
    ui.editorPanel = roundedBox(lv_screen_active(), 5, 17, 230, 102, 10, rgb(20, 18, 23));
    lv_obj_set_style_border_color(ui.editorPanel, accent(), 0);
    lv_obj_set_style_border_width(ui.editorPanel, 1, 0);
    ui.editorTitle = label(ui.editorPanel, "", &font_cn_16, labelColor());
    place(ui.editorTitle, 10, 6);
    t = label(ui.editorPanel, "ESC 取消", &font_cn_10, secondary());
    place(t, 160, 9);
    ui.editorBox = roundedBox(ui.editorPanel, 7, 29, 216, 34, 5, rgb(10, 9, 12));
    ui.editorValue = label(ui.editorPanel, "", &font_cn_10, labelColor());
    place(ui.editorValue, 13, 35);
    clipped(ui.editorValue, 202);
    ui.editorCursor = roundedBox(ui.editorPanel, 13, 35, 1, 12, 0, accent());
    ui.editorHint = label(ui.editorPanel, "", &font_cn_10, secondary());
    place(ui.editorHint, 11, 71);
    t = label(ui.editorPanel, "ENTER 保存", &font_cn_10, accent());
    place(t, 156, 82);

    // Toast
    ui.toast = roundedBox(lv_screen_active(), 60, 105, 120, 22, 9, rgb(38, 35, 40));
    ui.toastLabel = label(ui.toast, "", &font_cn_16, rgb(255, 255, 255));
    lv_obj_center(ui.toastLabel);
}

// ---------------------------------------------------------------- render
void renderStatus() {
    recolor(ui.statusName, secondary());
    int level = board_battery_level();
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    const lv_color_t tint = level < 20 ? accent() : secondary();
    lv_obj_set_style_border_color(ui.batteryFrame, tint, 0);
    fill(ui.batteryTip, tint);
    fill(ui.batteryFill, tint);
    const int w = level * 12 / 100;
    lv_obj_set_width(ui.batteryFill, w > 1 ? w : 1);
}

void renderHome() {
    const Track* current = currentTrack();
    fill(ui.homeHero, blend(card(), accent(), Settings.dark ? 0.16f : 0.08f));
    const bool hasContent = Audio.hasSelection() || Library.count();
    if (hasContent) {
        lv_obj_clear_flag(ui.homeEmpty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.homeEmpty, LV_OBJ_FLAG_HIDDEN);
        const Track* track = current ? current : (Audio.onlineActive() ? nullptr : Library.track(0));
        const uint32_t seed = track ? track->seed : Audio.onlineSeed();
        const std::string title = track ? track->title : Audio.onlineTitle();
        const int tick = Audio.playing() ? (int)(millis() / 180) : -1;
        if (ui.coverSeed[0] != seed || ui.coverPlayingTick[0] != tick) {
            drawCover(ui.homeCover, 26, seed, Audio.playing());
            ui.coverSeed[0] = seed;
            ui.coverPlayingTick[0] = tick;
        }
        setText(ui.homeKicker, Audio.onlineActive() ? "网易云在线"
                                                    : (current ? "继续播放" : "为 Cardputer 打造"));
        setText(ui.homeTitle, title);
        setText(ui.homePlay, Audio.playing() ? "||" : ">");
        lv_obj_clear_flag(ui.homeCover, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.homeKicker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.homeTitle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.homePlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(ui.homeEmpty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.homeCover, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.homeKicker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.homeTitle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.homePlay, LV_OBJ_FLAG_HIDDEN);
    }
    const int quickRows = Audio.hasSelection() ? 1 : 2;
    for (int r = 0; r < 2; ++r) {
        const bool visible = r < quickRows && r < (int)Library.count();
        if (!visible) {
            lv_obj_add_flag(ui.quickRow[r], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.quickDot[r], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.quickTitle[r], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.quickHeart[r], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(ui.quickRow[r], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.quickDot[r], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.quickTitle[r], LV_OBJ_FLAG_HIDDEN);
        const Track* track = Library.track(r);
        const bool selected = App.selected == r;
        fill(ui.quickRow[r], selected ? blend(card(), accent(), 0.17f) : background());
        fill(ui.quickDot[r], r ? accent() : secondary());
        setText(ui.quickTitle[r], track->title);
        recolor(ui.quickTitle[r], selected ? labelColor() : secondary());
        if (Settings.isFavorite(track->seed)) lv_obj_clear_flag(ui.quickHeart[r], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(ui.quickHeart[r], LV_OBJ_FLAG_HIDDEN);
    }
}

void renderLibraryPage(bool favorites) {
    const int idx = favorites ? 1 : 0;
    const int count = App.listCount();
    setText(ui.libCount[idx], std::to_string(count) + " 首");
    if (!favorites) setText(ui.libSourceState[idx], Library.ready() ? "SD 卡" : "未插卡");
    if (!count) {
        lv_obj_clear_flag(ui.libEmpty[idx], LV_OBJ_FLAG_HIDDEN);
        for (int r = 0; r < 3; ++r) {
            lv_obj_add_flag(ui.libRow[idx][r], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.libIcon[idx][r], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.libRowTitle[idx][r], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.libHeart[idx][r], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.libDuration[idx][r], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(ui.libScroll[idx], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(ui.libEmpty[idx], LV_OBJ_FLAG_HIDDEN);
    auto favoriteList = favorites ? App.favoriteIndices() : std::vector<int>();
    const int rows = favorites ? 3 : 2;
    const int startY = favorites ? 35 : 55;
    int first = App.selected - rows + 1;
    if (first < 0) first = 0;
    if (first > count - rows && count > rows) first = count - rows;
    if (first < 0) first = 0;
    for (int r = 0; r < 3; ++r) {
        const bool visible = r < rows && first + r < count;
        lv_obj_t* widgets[] = {ui.libRow[idx][r], ui.libIcon[idx][r], ui.libRowTitle[idx][r],
                               ui.libHeart[idx][r], ui.libDuration[idx][r]};
        if (!visible) {
            for (lv_obj_t* w : widgets) lv_obj_add_flag(w, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const int listIndex = first + r;
        const int trackIndex = favorites ? favoriteList[listIndex] : listIndex;
        const Track* track = Library.track(trackIndex);
        const bool selected = listIndex == App.selected;
        const bool playing = trackIndex == Audio.currentTrack();
        fill(ui.libRow[idx][r], selected ? blend(card(), accent(), 0.18f) : background());
        fill(ui.libIcon[idx][r], blend(card(), accent(), playing ? 0.85f : 0.20f));
        setText(ui.libRowTitle[idx][r], track->title);
        recolor(ui.libRowTitle[idx][r], selected || playing ? labelColor() : secondary());
        if (Settings.isFavorite(track->seed)) lv_obj_clear_flag(ui.libHeart[idx][r], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(ui.libHeart[idx][r], LV_OBJ_FLAG_HIDDEN);
        setText(ui.libDuration[idx][r], timeString(track->duration));
        for (lv_obj_t* w : widgets) lv_obj_clear_flag(w, LV_OBJ_FLAG_HIDDEN);
    }
    if (count > rows) {
        lv_obj_clear_flag(ui.libScroll[idx], LV_OBJ_FLAG_HIDDEN);
        const int trackHeight = rows * 17;
        const int barY = startY + (trackHeight - 16) * App.selected / (count - 1 > 0 ? count - 1 : 1);
        place(ui.libScroll[idx], 236, barY);
    } else {
        lv_obj_add_flag(ui.libScroll[idx], LV_OBJ_FLAG_HIDDEN);
    }
}

void renderSettings() {
    const char* names[14] = {"深色外观", "显示歌词", "减少动态效果", "播放模式", "屏幕亮度",
                             "重新扫描曲库", "WebUI 地址", "网易云登录", "网易云 API", "AP 名称",
                             "AP 密码", "Wi-Fi 名称", "Wi-Fi 密码", "重启应用配置"};
    const std::string values[14] = {
        Settings.dark ? "开启" : "关闭", Settings.showLyrics ? "开启" : "关闭",
        Settings.reduceMotion ? "开启" : "关闭",
        Settings.playMode == PlayMode::Sequential ? "顺序"
            : (Settings.playMode == PlayMode::RepeatOne ? "单曲" : "随机"),
        std::to_string(Settings.brightness * 100 / 255) + "%", "ENTER", Web.address(),
        Netease.loggedIn() ? Netease.nickname() : "扫码 / 手机号", Settings.neteaseApi,
        Settings.apSsid, !Settings.apPassword.empty() ? "********" : "未设置",
        !Settings.staSsid.empty() ? Settings.staSsid : "未设置",
        !Settings.staPassword.empty() ? "********" : "开放/未设置", "ENTER"};
    int first = App.settingsCursor - 2;
    if (first < 0) first = 0;
    if (first > 11) first = 11;
    for (int r = 0; r < 3; ++r) {
        const int index = first + r;
        const int y = 35 + r * 18;
        const bool selected = index == App.settingsCursor;
        fill(ui.setRow[r], selected ? blend(card(), accent(), 0.18f) : card());
        setText(ui.setName[r], names[index]);
        recolor(ui.setName[r], selected ? labelColor() : secondary());
        setText(ui.setValue[r], values[index]);
        recolor(ui.setValue[r], selected ? accent() : secondary());
        place(ui.setRow[r], 7, y - 2);
        place(ui.setName[r], 13, y - 1);
        const int valueWidth = index >= 6 && index <= 12 ? 92 : 51;
        lv_obj_set_width(ui.setValue[r], valueWidth);
        lv_obj_set_style_text_align(ui.setValue[r], LV_TEXT_ALIGN_RIGHT, 0);
        place(ui.setValue[r], 225 - valueWidth, y + 1);
        const bool brightness = index == 4;
        if (brightness) {
            lv_obj_clear_flag(ui.setBrightBar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui.setBrightFill, LV_OBJ_FLAG_HIDDEN);
            place(ui.setBrightBar, 151, y + 5);
            place(ui.setBrightFill, 151, y + 5);
            lv_obj_set_width(ui.setBrightFill, Settings.brightness * 42 / 255);
            fill(ui.setBrightBar, blend(card(), secondary(), 0.25f));
        }
    }
    if (first + 0 != 4 && first + 1 != 4 && first + 2 != 4) {
        lv_obj_add_flag(ui.setBrightBar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.setBrightFill, LV_OBJ_FLAG_HIDDEN);
    }
    place(ui.setScroll, 236, 35 + App.settingsCursor * 5);
}

void renderMiniAndTabs() {
    if (Audio.hasSelection()) {
        lv_obj_clear_flag(ui.mini, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.miniCover, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.miniTitle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.miniPlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.miniNext, LV_OBJ_FLAG_HIDDEN);
        const uint32_t seed = currentSeed();
        if (ui.coverSeed[1] != seed) {
            drawCover(ui.miniCover, 16, seed, false);
            ui.coverSeed[1] = seed;
        }
        setText(ui.miniTitle, currentTitle());
        if (Audio.state() == AudioState::Loading) lv_obj_clear_flag(ui.miniLoading, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(ui.miniLoading, LV_OBJ_FLAG_HIDDEN);
        setText(ui.miniPlay, Audio.playing() ? "||" : ">");
        fill(ui.mini, blend(card(), background(), 0.18f));
    } else {
        lv_obj_add_flag(ui.mini, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.miniCover, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.miniTitle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.miniLoading, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.miniPlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.miniNext, LV_OBJ_FLAG_HIDDEN);
    }
    fill(ui.tabBar, blend(card(), background(), 0.28f));
    for (int i = 0; i < 4; ++i)
        recolor(ui.tabLabel[i], i == (uint8_t)App.screen ? accent() : secondary());
}

void renderPlayer() {
    const bool online = Audio.onlineActive();
    const bool selected = currentTrack() || online;
    const uint32_t seed = currentSeed();
    const lv_color_t white = Settings.dark ? rgb(255, 255, 255) : rgb(30, 25, 30);
    const lv_color_t dark = Settings.dark ? rgb(8, 7, 10) : rgb(238, 235, 240);
    lv_color_t p[3];
    palette(seed, p);
    const float time = Settings.reduceMotion ? 0.0f : millis() / 4200.0f;
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 6; ++col) {
            const float wave = 0.5f + 0.5f * sinf(time + col * 0.83f + row * 0.61f + (seed & 15));
            const lv_color_t tint = blend(p[col % 3], p[(col + row + 1) % 3], wave);
            const float strength = 0.28f + 0.22f * (1.0f - fabsf(col - 2.5f) / 3.0f);
            fill(ui.mesh[row * 6 + col], blend(dark, tint, strength));
        }
    }
    setText(ui.playerTitle, online ? "网易云 · 正在播放" : "正在播放");
    recolor(ui.playerTitle, white);
    recolor(ui.playerHeader, white);
    recolor(ui.playerQueueIcon, white);

    const bool lyricsMode = selected && App.lyricsMode;
    const int tick = Audio.playing() ? (int)(millis() / 180) : -1;
    if (lyricsMode) {
        if (ui.coverSeed[2] != seed || ui.coverPlayingTick[2] != tick) {
            drawCover(ui.playerCoverSmall, 56, seed, Audio.playing());
            ui.coverSeed[2] = seed;
            ui.coverPlayingTick[2] = tick;
        }
        lv_obj_clear_flag(ui.playerCoverSmall, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.playerCoverBig, LV_OBJ_FLAG_HIDDEN);
        setText(ui.playerSongTitle, currentTitle());
        recolor(ui.playerSongTitle, white);
        lv_obj_clear_flag(ui.playerSongTitle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.playerSongTitleBig, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.playerEmpty, LV_OBJ_FLAG_HIDDEN);
    } else if (selected) {
        if (ui.coverSeed[3] != seed || ui.coverPlayingTick[3] != tick) {
            drawCover(ui.playerCoverBig, 64, seed, Audio.playing());
            ui.coverSeed[3] = seed;
            ui.coverPlayingTick[3] = tick;
        }
        lv_obj_add_flag(ui.playerCoverSmall, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.playerCoverBig, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.playerSongTitle, LV_OBJ_FLAG_HIDDEN);
        setText(ui.playerSongTitleBig, currentTitle());
        recolor(ui.playerSongTitleBig, white);
        lv_obj_clear_flag(ui.playerSongTitleBig, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.playerEmpty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.playerCoverSmall, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.playerCoverBig, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.playerSongTitle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.playerSongTitleBig, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.playerEmpty, LV_OBJ_FLAG_HIDDEN);
    }

    // Lyrics
    if (lyricsMode) {
        if (millis() - ui.lastLyricSnapshot > 100 || ui.lyricSnapshot.trackSeed != seed) {
            Lyrics.snapshot(Audio.position() * 1000, ui.lyricSnapshot);
            ui.lastLyricSnapshot = millis();
        }
        const LrcView& view = ui.lyricSnapshot.view;
        if (!view.available || ui.lyricSnapshot.trackSeed != seed) {
            for (int i = 0; i < 5; ++i) lv_obj_add_flag(ui.lyricLine[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.lyricClip, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui.lyricNone, LV_OBJ_FLAG_HIDDEN);
            setText(ui.lyricHint, currentTrack() ? "添加同名 .lrc 文件即可显示" : "在线歌曲可能没有歌词");
            lv_obj_clear_flag(ui.lyricHint, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui.lyricNone, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.lyricHint, LV_OBJ_FLAG_HIDDEN);
            if (view.activeLine != ui.lyricIndex || ui.lyricSnapshot.trackSeed != ui.lyricSeed) {
                ui.lyricIndex = view.activeLine;
                ui.lyricSeed = ui.lyricSnapshot.trackSeed;
                ui.lyricChangeAt = millis();
            }
            float travel = 0;
            if (!Settings.reduceMotion) {
                const float t = (millis() - ui.lyricChangeAt) / 400.0f;
                const float clamped = t < 0 ? 0 : (t > 1 ? 1 : t);
                travel = (1.0f - (1.0f - powf(1.0f - clamped, 3))) * 11.0f;
            }
            const lv_color_t dim = Settings.dark ? rgb(130, 125, 135) : rgb(85, 75, 90);
            for (int i = 0; i < 5; ++i) {
                const int slot = i + 1;  // relative -2..+2 occupy slots 1..5
                const LrcViewLine& line = view.lines[slot];
                if (!line.valid || line.relative < -2 || line.relative > 2) {
                    lv_obj_add_flag(ui.lyricLine[i], LV_OBJ_FLAG_HIDDEN);
                    continue;
                }
                const int y = 26 + (line.relative + 2) * 13 + (int)travel;
                place(ui.lyricLine[i], 79, y);
                setText(ui.lyricLine[i], line.text);
                const bool active = slot == view.active;
                lv_obj_set_style_text_font(ui.lyricLine[i], active ? &font_cn_16 : &font_cn_10, 0);
                recolor(ui.lyricLine[i], active ? blend(dim, accent(), 0.65f) : dim);
                lv_obj_clear_flag(ui.lyricLine[i], LV_OBJ_FLAG_HIDDEN);
                if (active) {
                    uint32_t elapsed = Audio.position() * 1000;
                    float progress = 1.0f;
                    if (line.endMs > line.startMs && elapsed > line.startMs) {
                        const uint32_t span = line.endMs - line.startMs;
                        const uint32_t done = elapsed - line.startMs < span ? elapsed - line.startMs : span;
                        progress = (float)done / span;
                    } else if (line.endMs > line.startMs) {
                        progress = 0.0f;
                    }
                    const int32_t width = lv_text_get_width(line.text.c_str(), line.text.size(),
                                                            &font_cn_16, 0);
                    const int clipW = (int)(width * progress);
                    place(ui.lyricClip, 79, y);
                    lv_obj_set_size(ui.lyricClip, clipW > 1 ? clipW : 1, 16);
                    setText(ui.lyricBright, line.text);
                    lv_obj_clear_flag(ui.lyricClip, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(ui.lyricClip, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    } else {
        for (int i = 0; i < 5; ++i) lv_obj_add_flag(ui.lyricLine[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lyricClip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lyricNone, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lyricHint, LV_OBJ_FLAG_HIDDEN);
    }

    // Transport
    const lv_color_t shelfColor = Settings.dark ? rgb(18, 16, 20) : rgb(244, 240, 245);
    fill(ui.shelf, shelfColor);
    const uint32_t position = Audio.position();
    const uint32_t duration = Audio.duration();
    fill(ui.progressBg, blend(shelfColor, secondary(), 0.35f));
    lv_obj_set_width(ui.progressFill, duration ? (position * 222 / duration > 0 ? position * 222 / duration : 1) : 1);
    setText(ui.timePos, timeString(position));
    setText(ui.timeDur, timeString(duration));
    const Track* track = currentTrack();
    const bool favorite = track && Settings.isFavorite(track->seed);
    setText(ui.playerHeart, favorite ? "♥" : "♡");
    recolor(ui.playerHeart, favorite ? accent() : secondary());
    recolor(ui.playerPrev, labelColor());
    recolor(ui.playerNext, labelColor());
    fill(ui.playCircle, labelColor());
    setText(ui.playerPlay, Audio.playing() ? "||" : ">");
    recolor(ui.playerPlay, background());
    recolor(ui.playerQueue, App.queueOpen ? accent() : secondary());
    fill(ui.volumeBg, blend(shelfColor, secondary(), 0.30f));
    lv_obj_set_width(ui.volumeFill, Settings.volume * 38 / 20);
}

void renderQueue() {
    if (!App.queueOpen) {
        lv_obj_add_flag(ui.queuePanel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(ui.queuePanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.queuePanel);
    fill(ui.queuePanel, Settings.dark ? rgb(26, 25, 29) : rgb(250, 249, 252));
    const int count = App.listCount();
    if (!count) {
        lv_obj_clear_flag(ui.queueEmpty, LV_OBJ_FLAG_HIDDEN);
        for (int r = 0; r < 5; ++r) {
            lv_obj_add_flag(ui.queueRow[r], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.queueNum[r], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.queueTitle[r], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.queueDuration[r], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_obj_add_flag(ui.queueEmpty, LV_OBJ_FLAG_HIDDEN);
    auto favorites = App.screen == Screen::Favorites ? App.favoriteIndices() : std::vector<int>();
    int first = App.selected - 2;
    if (first < 0) first = 0;
    if (first > count - 5 && count > 5) first = count - 5;
    if (first < 0) first = 0;
    for (int r = 0; r < 5; ++r) {
        const bool visible = first + r < count;
        if (!visible) {
            lv_obj_add_flag(ui.queueRow[r], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.queueNum[r], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.queueTitle[r], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.queueDuration[r], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const int listIndex = first + r;
        const int trackIndex = !favorites.empty() ? favorites[listIndex] : listIndex;
        const Track* track = Library.track(trackIndex);
        const bool selected = listIndex == App.selected;
        const bool current = trackIndex == Audio.currentTrack();
        fill(ui.queueRow[r], selected ? blend(Settings.dark ? rgb(26, 25, 29) : rgb(250, 249, 252),
                                              accent(), 0.20f)
                                      : Settings.dark ? rgb(26, 25, 29) : rgb(250, 249, 252));
        setText(ui.queueNum[r], std::to_string(listIndex + 1));
        recolor(ui.queueNum[r], current ? accent() : secondary());
        setText(ui.queueTitle[r], track->title);
        recolor(ui.queueTitle[r], current ? accent() : labelColor());
        setText(ui.queueDuration[r], timeString(track->duration));
        lv_obj_clear_flag(ui.queueRow[r], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.queueNum[r], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.queueTitle[r], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.queueDuration[r], LV_OBJ_FLAG_HIDDEN);
    }
}

void renderHelp() {
    if (!App.helpOpen) {
        lv_obj_add_flag(ui.helpPanel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(ui.helpPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.helpPanel);
    fill(ui.helpPanel, Settings.dark ? rgb(22, 22, 25) : rgb(252, 252, 253));
}

void drawQr(const std::string& value) {
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
    uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
    if (!qrcodegen_encodeText(value.c_str(), temp, code, qrcodegen_Ecc_LOW, 6, 6,
                              qrcodegen_Mask_AUTO, false)) {
        return;
    }
    const int size = qrcodegen_getSize(code);
    auto* pixels = (uint16_t*)qrBuf;
    for (int y = 0; y < 98; ++y)
        for (int x = 0; x < 98; ++x) pixels[y * 98 + x] = 0xFFFF;
    for (int row = 0; row < size; ++row) {
        for (int col = 0; col < size; ++col) {
            if (!qrcodegen_getModule(code, col, row)) continue;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int x = (col + 4) * 2 + dx;
                    const int y = (row + 4) * 2 + dy;
                    if (x < 98 && y < 98) pixels[y * 98 + x] = 0x0000;
                }
        }
    }
    lv_obj_invalidate(ui.qrCanvas);
}

void renderNetease() {
    if (!App.neteaseOpen) {
        lv_obj_add_flag(ui.neteasePanel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(ui.neteasePanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.neteasePanel);
    fill(ui.neteasePanel, Settings.dark ? rgb(18, 18, 22) : rgb(250, 250, 252));
    const bool qr = App.neteaseView == NeteaseView::Qr;
    const bool phone = App.neteaseView == NeteaseView::Phone;
    setText(ui.neteaseTitle, qr ? "网易云扫码登录" : (phone ? "网易云手机号登录" : "网易云账号"));

    // Hide everything, then show the active view.
    lv_obj_add_flag(ui.neteaseMessage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.neteaseHint, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 6; ++i) {
        lv_obj_add_flag(ui.neteaseRow[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.neteaseRowName[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.neteaseRowValue[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(ui.qrCanvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.qrPlaceholder, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.qrStatus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.qrHint1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.qrHint2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.qrWarn1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.qrWarn2, LV_OBJ_FLAG_HIDDEN);

    if (qr) {
        if (!App.neteaseQrUrl.empty()) {
            if (ui.lastQrUrl != App.neteaseQrUrl) {
                drawQr(App.neteaseQrUrl);
                ui.lastQrUrl = App.neteaseQrUrl;
            }
            lv_obj_clear_flag(ui.qrCanvas, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(ui.qrPlaceholder, LV_OBJ_FLAG_HIDDEN);
        }
        setText(ui.qrStatus, App.neteaseQrStatus);
        recolor(ui.qrStatus, App.neteaseQrCode == 803 ? accent() : labelColor());
        setText(ui.qrHint1, App.neteaseQrActive ? "请用网易云 App 扫码" : "ENTER 重新生成 / 返回");
        setText(ui.qrHint2, App.neteaseQrActive ? "每 2 秒自动刷新状态" : "R 也可重新生成");
        lv_obj_clear_flag(ui.qrStatus, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.qrHint1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.qrHint2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.qrWarn1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.qrWarn2, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    setText(ui.neteaseMessage, App.neteaseMessage);
    recolor(ui.neteaseMessage, App.neteaseBusy ? accent() : secondary());
    lv_obj_clear_flag(ui.neteaseMessage, LV_OBJ_FLAG_HIDDEN);
    if (phone) {
        const char* names[5] = {"地区码", "手机号", "密码", "登录", "返回"};
        const std::string values[5] = {
            App.neteaseCountry, !App.neteasePhone.empty() ? App.neteasePhone : "未输入",
            !App.neteasePassword.empty() ? "已输入（不保存）" : "未输入", "ENTER", "ENTER"};
        for (int i = 0; i < 5; ++i) {
            const int y = 42 + i * 13;
            const bool selected = i == App.neteaseCursor;
            place(ui.neteaseRow[i], 8, y - 2);
            fill(ui.neteaseRow[i], selected ? blend(rgb(18, 18, 22), accent(), 0.18f)
                                            : rgb(18, 18, 22));
            setText(ui.neteaseRowName[i], names[i]);
            recolor(ui.neteaseRowName[i], selected ? labelColor() : secondary());
            setText(ui.neteaseRowValue[i], values[i]);
            recolor(ui.neteaseRowValue[i], selected ? accent() : secondary());
            place(ui.neteaseRowName[i], 14, y);
            place(ui.neteaseRowValue[i], 150, y);
            lv_obj_clear_flag(ui.neteaseRow[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui.neteaseRowName[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui.neteaseRowValue[i], LV_OBJ_FLAG_HIDDEN);
        }
        setText(ui.neteaseHint, "W/S 选择 · ENTER 编辑或执行");
        lv_obj_clear_flag(ui.neteaseHint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const char* names[6] = {"扫码登录", "手机号密码登录", "刷新登录状态", "退出登录",
                            "修改 API 地址", "返回设置"};
    for (int i = 0; i < 6; ++i) {
        const int y = 41 + i * 12;
        const bool selected = i == App.neteaseCursor;
        place(ui.neteaseRow[i], 8, y - 2);
        fill(ui.neteaseRow[i], selected ? blend(rgb(18, 18, 22), accent(), 0.18f)
                                        : rgb(18, 18, 22));
        setText(ui.neteaseRowName[i], names[i]);
        recolor(ui.neteaseRowName[i], selected ? labelColor() : secondary());
        setText(ui.neteaseRowValue[i], selected ? "ENTER" : "");
        recolor(ui.neteaseRowValue[i], accent());
        place(ui.neteaseRowName[i], 14, y);
        place(ui.neteaseRowValue[i], 186, y);
        lv_obj_clear_flag(ui.neteaseRow[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.neteaseRowName[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.neteaseRowValue[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(ui.neteaseHint, LV_OBJ_FLAG_HIDDEN);
}

void renderEditor() {
    if (!App.textEditOpen) {
        lv_obj_add_flag(ui.editorPanel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(ui.editorPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.editorPanel);
    fill(ui.editorPanel, Settings.dark ? rgb(20, 18, 23) : rgb(252, 252, 253));
    const char* title = "设置 AP 名称";
    const char* hint = "1–32 字节；支持大小写";
    switch (App.textEditTarget) {
        case TextEditTarget::ApPassword: title = "设置 AP 密码"; hint = "8–63 位；区分大小写"; break;
        case TextEditTarget::StaSsid: title = "设置 Wi-Fi 名称"; break;
        case TextEditTarget::StaPassword: title = "设置 Wi-Fi 密码"; hint = "开放网络可留空；否则 8–63 位"; break;
        case TextEditTarget::NeteaseApi: title = "设置网易云 API 地址"; hint = "http(s)://主机:端口；最长 160 字节"; break;
        case TextEditTarget::NeteaseCountry: title = "网易云登录地区码"; hint = "中国大陆为 86；仅输入数字"; break;
        case TextEditTarget::NeteasePhone: title = "网易云登录手机号"; hint = "仅输入数字；不会写入配置"; break;
        case TextEditTarget::NeteasePassword: title = "网易云密码（不保存）"; hint = "仅用于本次登录；请求后清空"; break;
        default: break;
    }
    setText(ui.editorTitle, title);
    setText(ui.editorHint, hint);
    std::string visible = App.textEditSecret ? "********" : App.textEditValue;
    if (visible.size() > 28) {
        size_t start = visible.size() - 28;
        while (start < visible.size() && ((uint8_t)visible[start] & 0xc0) == 0x80) ++start;
        visible = visible.substr(start);
    }
    setText(ui.editorValue, !visible.empty() ? visible : "请输入…");
    recolor(ui.editorValue, !visible.empty() ? labelColor() : secondary());
    const int32_t textWidth = lv_text_get_width(visible.c_str(), visible.size(), &font_cn_10, 0);
    place(ui.editorCursor, 13 + (textWidth < 198 ? textWidth : 198) + 2, 35);
    if ((millis() / 450) % 2 == 0) lv_obj_clear_flag(ui.editorCursor, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ui.editorCursor, LV_OBJ_FLAG_HIDDEN);
}

void renderToast() {
    if (App.toast.empty()) {
        lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.toast);
    setText(ui.toastLabel, App.toast);
    const int32_t textWidth = lv_text_get_width(App.toast.c_str(), App.toast.size(), &font_cn_16, 0);
    int width = textWidth + 22;
    if (width < 70) width = 70;
    if (width > 216) width = 216;
    lv_obj_set_width(ui.toast, width);
    place(ui.toast, (240 - width) / 2, 105);
    lv_obj_center(ui.toastLabel);
}

void applyTheme() {
    fill(ui.browse, background());
    // Re-color static labels that depend on the theme.
    lv_obj_t* screen = lv_screen_active();
    (void)screen;
}

}  // namespace

void ui_begin() {
    lv_obj_set_style_bg_color(lv_screen_active(), background(), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    lv_obj_clear_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
    buildBrowse();
    buildPlayer();
    buildOverlays();
    ui.lastDark = Settings.dark;
    ui.lastScreen = App.screen;
    ui.lastPlayerOpen = App.playerOpen;
    lv_obj_set_y(ui.player, App.playerOpen ? 0 : 135);
    lv_obj_add_flag(ui.player, LV_OBJ_FLAG_HIDDEN);
}

void ui_render() {
    const uint32_t now = millis();
    if (now - ui.lastFrame < (App.playerOpen ? 33u : 50u)) return;
    ui.lastFrame = now;

    if (App.playerOpen != ui.lastPlayerOpen) {
        animatePlayer(App.playerOpen);
        ui.lastPlayerOpen = App.playerOpen;
        if (App.playerOpen) lv_obj_clear_flag(ui.player, LV_OBJ_FLAG_HIDDEN);
    }
    if (App.screen != ui.lastScreen) {
        animateTab((int)App.screen);
        ui.lastScreen = App.screen;
    }
    if (Settings.dark != ui.lastDark) {
        ui.lastDark = Settings.dark;
        lv_obj_set_style_bg_color(lv_screen_active(), background(), 0);
        applyTheme();
        // Force cover redraws with the new palette context.
        for (int i = 0; i < 4; ++i) ui.coverSeed[i] = 0xFFFFFFFF;
    }

    const bool playerVisible = App.playerOpen || lv_obj_get_y(ui.player) < 135;
    if (!App.playerOpen && lv_obj_get_y(ui.player) >= 135)
        lv_obj_add_flag(ui.player, LV_OBJ_FLAG_HIDDEN);

    if (!playerVisible || lv_obj_get_y(ui.player) > 0) {
        lv_obj_clear_flag(ui.browse, LV_OBJ_FLAG_HIDDEN);
        fill(ui.browse, background());
        renderStatus();
        for (int i = 0; i < 4; ++i) {
            if (i == (int)App.screen) lv_obj_clear_flag(ui.pages[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(ui.pages[i], LV_OBJ_FLAG_HIDDEN);
        }
        switch (App.screen) {
            case Screen::Home: renderHome(); break;
            case Screen::Library: renderLibraryPage(false); break;
            case Screen::Favorites: renderLibraryPage(true); break;
            case Screen::Settings: renderSettings(); break;
        }
        renderMiniAndTabs();
    } else {
        lv_obj_add_flag(ui.browse, LV_OBJ_FLAG_HIDDEN);
    }
    if (playerVisible) {
        lv_obj_clear_flag(ui.player, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ui.player);
        renderPlayer();
    }
    renderQueue();
    renderHelp();
    renderNetease();
    renderEditor();
    renderToast();
}

}  // namespace lz
