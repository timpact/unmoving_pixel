# dash PC simulator (ADR 0010): builds dash/ui natively on macOS with SDL2.
#
#   cmake -S dash/sim -B dash/sim/build -G Ninja
#   cmake --build dash/sim/build
#   dash/sim/build/dash_sim                       # window, mouse = touch
#   dash/sim/build/dash_sim --screenshot <dir>    # headless, writes BMP screenshots
cmake_minimum_required(VERSION 3.16)
project(dash_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(DASH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(REPO_DIR ${DASH_DIR}/..)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

# ---- LVGL (same version as the firmware) ----
include(FetchContent)
set(LV_BUILD_CONF_PATH ${DASH_DIR}/ui/lv_conf.h CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.3.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(lvgl)
target_compile_definitions(lvgl PUBLIC UP_SIM=1)
target_link_libraries(lvgl PUBLIC PkgConfig::SDL2)

# ---- Font: embed Montserrat TTF as C array ----
set(FONT_TTF ${DASH_DIR}/ui/assets/Montserrat-Medium.ttf)
set(FONT_C ${CMAKE_CURRENT_BINARY_DIR}/ui_font_data.c)
add_custom_command(
    OUTPUT ${FONT_C}
    COMMAND ${CMAKE_COMMAND} -DIN=${FONT_TTF} -DOUT=${FONT_C}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_file.cmake
    DEPENDS ${FONT_TTF} ${CMAKE_CURRENT_SOURCE_DIR}/embed_file.cmake
    COMMENT "Embedding Montserrat-Medium.ttf")

add_executable(dash_sim
    main.c
    sim_core.c
    headless.c
    png.c
    check.c
    ${DASH_DIR}/ui/ui.c
    ${DASH_DIR}/ui/ui_ride.c
    ${DASH_DIR}/ui/ui_led.c
    ${DASH_DIR}/ui/ui_settings.c
    ${DASH_DIR}/app/measurement.c
    ${FONT_C})

target_include_directories(dash_sim PRIVATE
    ${DASH_DIR}/ui
    ${DASH_DIR}/app
    ${REPO_DIR}/common/include)

target_compile_options(dash_sim PRIVATE -Wall -Wextra -Wno-unused-parameter)
find_package(ZLIB REQUIRED)
target_link_libraries(dash_sim PRIVATE lvgl PkgConfig::SDL2 ZLIB::ZLIB m)

# GUI checks (ADR 0016): approved references live in the repository,
# results and diff images in the build directory
target_compile_definitions(dash_sim PRIVATE
    SIM_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference"
    SIM_CHECK_OUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/check")
