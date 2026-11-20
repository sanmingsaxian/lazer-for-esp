#include "keyboard.h"
#include "pins.h"

#include <driver/gpio.h>
#include <esp_rom_sys.h>

namespace lz {

namespace {
// 74HC138 address lines and the seven column inputs (active low).
constexpr int kOutputs[3] = {pins::KB_A0, pins::KB_A1, pins::KB_A2};

struct KeyCoord {
    int x;
    int y;
};

// X mapping from M5Cardputer IOMatrixKeyboardReader: address > 3 uses x_1,
// otherwise x_2; row y = 3 - (address > 3 ? address - 4 : address).
constexpr int kX1[7] = {0, 2, 4, 6, 8, 10, 12};
constexpr int kX2[7] = {1, 3, 5, 7, 9, 11, 13};

constexpr char KEY_BS = 0x2a;
constexpr char KEY_TAB = 0x2b;
constexpr char KEY_ENTER = 0x28;
constexpr char KEY_CTRL = 0x80;
constexpr char KEY_SHIFT = 0x81;
constexpr char KEY_ALT = 0x82;
constexpr char KEY_FN = 0xff;
constexpr char KEY_OPT = 0x00;

// _key_value_map[y][x] = {first, second(shift)} from M5Cardputer Keyboard.h.
struct KeyValue {
    char first;
    char second;
};
const KeyValue kMap[4][14] = {
    {{'`', '~'}, {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'}, {'6', '^'},
     {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'}, {'-', '_'}, {'=', '+'}, {KEY_BS, KEY_BS}},
    {{KEY_TAB, KEY_TAB}, {'q', 'Q'}, {'w', 'W'}, {'e', 'E'}, {'r', 'R'}, {'t', 'T'}, {'y', 'Y'},
     {'u', 'U'}, {'i', 'I'}, {'o', 'O'}, {'p', 'P'}, {'[', '{'}, {']', '}'}, {'\\', '|'}},
    {{KEY_FN, KEY_FN}, {KEY_SHIFT, KEY_SHIFT}, {'a', 'A'}, {'s', 'S'}, {'d', 'D'}, {'f', 'F'},
     {'g', 'G'}, {'h', 'H'}, {'j', 'J'}, {'k', 'K'}, {'l', 'L'}, {';', ':'}, {'\'', '\"'},
     {KEY_ENTER, KEY_ENTER}},
    {{KEY_CTRL, KEY_CTRL}, {KEY_OPT, KEY_OPT}, {KEY_ALT, KEY_ALT}, {'z', 'Z'}, {'x', 'X'},
     {'c', 'C'}, {'v', 'V'}, {'b', 'B'}, {'n', 'N'}, {'m', 'M'}, {',', '<'}, {'.', '>'},
     {'/', '?'}, {' ', ' '}},
};

std::vector<KeyCoord> s_keys;
size_t s_lastSize = 0;
bool s_changed = false;
KeyState s_state;

void setAddress(uint8_t address) {
    gpio_set_level((gpio_num_t)kOutputs[0], address & 1);
    gpio_set_level((gpio_num_t)kOutputs[1], (address >> 1) & 1);
    gpio_set_level((gpio_num_t)kOutputs[2], (address >> 2) & 1);
    esp_rom_delay_us(10);  // let the demux outputs settle before sampling
}

void scan() {
    s_keys.clear();
    for (int address = 0; address < 8; ++address) {
        setAddress((uint8_t)address);
        uint8_t inputs = 0;
        for (int j = 0; j < 7; ++j) {
            if (gpio_get_level((gpio_num_t)pins::KB_INPUTS[j]) == 0)
                inputs |= (uint8_t)(1 << j);
        }
        if (!inputs) continue;
        for (int j = 0; j < 7; ++j) {
            if (inputs & (1 << j)) {
                KeyCoord coord;
                coord.x = address > 3 ? kX1[j] : kX2[j];
                coord.y = 3 - (address > 3 ? address - 4 : address);
                s_keys.push_back(coord);
            }
        }
    }
}

void buildState() {
    s_state = KeyState{};
    // Pass 1: modifiers first so shift state is known for every printable key.
    for (const auto& coord : s_keys) {
        const char code = kMap[coord.y][coord.x].first;
        switch (code) {
            case KEY_FN: s_state.fn = true; break;
            case KEY_OPT: s_state.opt = true; break;
            case KEY_CTRL: s_state.ctrl = true; break;
            case KEY_SHIFT: s_state.shift = true; break;
            case KEY_ALT: s_state.alt = true; break;
            default: break;
        }
    }
    // Pass 2: special keys and printable characters.
    for (const auto& coord : s_keys) {
        const KeyValue value = kMap[coord.y][coord.x];
        switch (value.first) {
            case KEY_FN:
            case KEY_OPT:
            case KEY_CTRL:
            case KEY_SHIFT:
            case KEY_ALT:
                continue;
            case KEY_TAB: s_state.tab = true; continue;
            case KEY_BS: s_state.del = true; continue;
            case KEY_ENTER: s_state.enter = true; continue;
            default: break;
        }
        if (value.first == ' ') s_state.space = true;
        const char out = (s_state.ctrl || s_state.shift) ? value.second : value.first;
        s_state.word.push_back(out);
    }
}
}  // namespace

void keyboard_begin() {
    for (int pin : kOutputs) {
        gpio_reset_pin((gpio_num_t)pin);
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)pin, 0);
    }
    for (int pin : pins::KB_INPUTS) {
        gpio_reset_pin((gpio_num_t)pin);
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
    }
    setAddress(0);
}

void keyboard_update() {
    scan();
    s_changed = s_keys.size() != s_lastSize;
    s_lastSize = s_keys.size();
    if (!s_keys.empty()) buildState();
    else s_state = KeyState{};
}

bool keyboard_changed() { return s_changed; }
bool keyboard_pressed() { return !s_keys.empty(); }
const KeyState& keyboard_state() { return s_state; }

}  // namespace lz
