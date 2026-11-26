# dash firmware (ADR 0019): board support, LVGL port, GUI from dash/ui
set(UI ../ui)
set(COMMON ../../common)

set(srcs
    "main.c" "board.c" "lvgl_port.c" "settings.c"
    "${UI}/ui.c" "${UI}/ui_ride.c" "${UI}/ui_led.c" "${UI}/ui_settings.c"
    "../app/measurement.c" "../app/dash_state.c"
    "${COMMON}/src/up_lights.c"
    "${COMMON}/src/up_proto.c")
if(CONFIG_DASH_DEMO_CORE)
    list(APPEND srcs "../sim/sim_core.c" "../app/preset_ram.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "." "${UI}" "../app" "../sim" "${COMMON}/include"
    EMBED_FILES "${UI}/assets/Montserrat-Medium.ttf"
    PRIV_REQUIRES esp_psram spi_flash esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_ledc
                  esp_hw_support esp_mm esp_timer nvs_flash
)

# Built-in presets from presets/ (ADR 0013), generated at build time
if(CONFIG_DASH_DEMO_CORE)
    set(REPO_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
    file(GLOB PRESET_FILES CONFIGURE_DEPENDS ${REPO_DIR}/presets/patterns/*.json ${REPO_DIR}/presets/overlays/*.json)
    set(PRESETS_C ${CMAKE_CURRENT_BINARY_DIR}/presets_builtin.c)
    add_custom_command(
        OUTPUT ${PRESETS_C}
        COMMAND ${PYTHON} ${REPO_DIR}/tools/gen_presets.py ${REPO_DIR}/presets ${PRESETS_C}
        DEPENDS ${PRESET_FILES} ${REPO_DIR}/tools/gen_presets.py
        COMMENT "Generating built-in presets")
    target_sources(${COMPONENT_LIB} PRIVATE ${PRESETS_C})
endif()
