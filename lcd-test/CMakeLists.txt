cmake_minimum_required(VERSION 3.16)

# Minimal LCD diagnostic project: reuses the shared components but excludes
# everything the display test does not need.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
set(EXCLUDE_COMPONENTS
    web wifi_mgr netease netease_audio audio_engine audio_out mp3dec
    library lyrics settings app storage keyboard ui qrcode common)
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults" "${CMAKE_CURRENT_LIST_DIR}/overrides.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lazer_lcd_test)
