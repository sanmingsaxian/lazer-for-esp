#pragma once
#include <cstdint>
#include <vector>

namespace lz {

// Mirrors M5Cardputer Keyboard_Class::KeysState fields used by the app.
struct KeyState {
    bool tab = false;
    bool fn = false;
    bool shift = false;
    bool ctrl = false;
    bool opt = false;
    bool alt = false;
    bool del = false;
    bool enter = false;
    bool space = false;
    std::vector<char> word;
};

void keyboard_begin();
// Scan the 74HC138 matrix; call once per main-loop iteration.
void keyboard_update();
bool keyboard_changed();
bool keyboard_pressed();
const KeyState& keyboard_state();

}  // namespace lz
