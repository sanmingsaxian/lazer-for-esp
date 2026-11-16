#pragma once

#include <Arduino.h>
#include <M5Cardputer.h>
#include "App.h"
#include "Lyrics.h"
#include "Motion.h"

class LazerUI {
public:
    void begin();
    void render();

private:
    enum class FontRole : uint8_t { Tiny, Body, Title };

    uint16_t color(uint8_t r, uint8_t g, uint8_t b) const;
    uint16_t blend(uint16_t a, uint16_t b, float t) const;
    uint16_t background() const;
    uint16_t card() const;
    uint16_t label() const;
    uint16_t secondary() const;
    uint16_t accent() const;

    void setFont(FontRole role, uint16_t foreground);
    void text(const String& value, int x, int y, int maxWidth, uint16_t foreground,
              FontRole role = FontRole::Body, int datum = TL_DATUM);
    void line(int x, int y, int w, uint16_t tint);
    void drawCover(int x, int y, int size, uint32_t seed, bool playing = false);
    void drawMesh(uint32_t seed, int yOffset = 0);
    void drawStatus();
    void drawBrowse(int xOffset = 0);
    void drawHome(int xOffset);
    void drawLibrary(int xOffset, bool favorites);
    void drawSettings(int xOffset);
    void drawMiniPlayer();
    void drawTabs();
    void drawNowPlaying(int yOffset);
    void drawTransport(int yOffset);
    void drawLyrics(int yOffset);
    void drawQueue();
    void drawHelp();
    void drawToast();
    void drawPlayIcon(int x, int y, bool playing, uint16_t tint, int scale = 1);
    void drawNextIcon(int x, int y, bool previous, uint16_t tint);
    void drawHeart(int x, int y, bool filled, uint16_t tint);
    void drawQueueIcon(int x, int y, uint16_t tint);
    void drawTabIcon(int tab, int x, int y, uint16_t tint);
    void palette(uint32_t seed, uint16_t* colors) const;
    String timeString(uint32_t seconds) const;
    const Track* currentTrack() const;

    M5Canvas canvas{&M5Cardputer.Display};
    uint32_t _lastFrame = 0;
    bool _lastPlayerOpen = false;
    Screen _lastScreen = Screen::Home;
    Spring _playerMotion;
    Spring _contentMotion;
    Spring _tabMotion;
    Spring _lyricMotion;
    uint32_t _lyricSeed = 0;
    int _lyricIndex = -1;
    LyricView _lyricView;
    uint32_t _lastLyricSnapshot = 0;
};

extern LazerUI UI;
