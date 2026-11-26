# dash – ESP32-P4 (Guition JC4880P443), dashboard display
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# LVGL includes "lv_conf.h" (LV_CONF_INCLUDE_SIMPLE): the same file as in the
# simulator, dash/ui/lv_conf.h (ADR 0010, 0019)
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_CURRENT_LIST_DIR}/ui" APPEND)
# Ignore LVGL's Kconfig (sdkconfig): only lv_conf.h counts, exactly as in the simulator
idf_build_set_property(COMPILE_OPTIONS "-DLV_KCONFIG_IGNORE" APPEND)

project(dash)
