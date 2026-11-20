#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "keyboard.h"

namespace lz {

enum class Screen : uint8_t { Home, Library, Favorites, Settings };
enum class TextEditTarget : uint8_t {
    ApSsid, ApPassword, StaSsid, StaPassword,
    NeteaseApi, NeteaseCountry, NeteasePhone, NeteasePassword
};
enum class NeteaseView : uint8_t { Menu, Qr, Phone };

// Port of LazerApp: keyboard-driven state machine shared by UI and WebUI.
class LazerApp {
public:
    void begin();
    void update();

    Screen screen = Screen::Home;
    int selected = 0;
    int settingsCursor = 0;
    bool playerOpen = false;
    bool lyricsMode = true;
    bool queueOpen = false;
    bool helpOpen = false;
    bool textEditOpen = false;
    bool textEditSecret = false;
    TextEditTarget textEditTarget = TextEditTarget::ApSsid;
    std::string textEditValue;

    bool neteaseOpen = false;
    NeteaseView neteaseView = NeteaseView::Menu;
    int neteaseCursor = 0;
    std::string neteaseQrUrl;
    std::string neteaseQrStatus;
    int neteaseQrCode = 0;
    bool neteaseQrActive = false;
    bool neteaseBusy = false;
    std::string neteaseMessage;
    std::string neteaseCountry = "86";
    std::string neteasePhone;
    std::string neteasePassword;

    std::string bootReset = "unknown";
    int bootResetCode = 0;
    std::string toast;
    uint32_t toastUntil = 0;

    std::vector<int> favoriteIndices() const;
    int listCount() const;
    int selectedTrack() const;
    void notify(const std::string& message, uint16_t milliseconds = 1800);
    void cyclePlayMode();

private:
    void handleKey(const KeyState& keys);
    void handleBrowserKey(const KeyState& keys, char key);
    void handlePlayerKey(const KeyState& keys, char key);
    void handleQueueKey(const KeyState& keys, char key);
    void handleSettingsKey(const KeyState& keys, char key);
    void handleTextEditKey(const KeyState& keys);
    void handleNeteaseKey(const KeyState& keys, char key);
    void beginTextEdit(TextEditTarget target);
    void openNetease(NeteaseView view = NeteaseView::Menu);
    void startNeteaseQr();
    void loginNeteasePhone();
    void selectRelative(int delta);
    void playSelected();
    void toggleSelectedFavorite();
    void changeVolume(int delta);
    void rescan();

    uint32_t _nextQrPoll = 0;
};

extern LazerApp App;

}  // namespace lz
