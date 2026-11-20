#pragma once

namespace lz {

// LVGL rebuild of the original M5GFX immediate-mode UI. Same screens,
// keyboard flow and information layout; animations use LVGL overshoot paths.
void ui_begin();
void ui_render();

}  // namespace lz
