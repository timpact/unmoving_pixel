# dash firmware (ADR 0019): board support, LVGL port, GUI from dash/ui
set(UI ../ui)
set(COMMON ../../common)

set(srcs
    "main.c" "board.c" "lvgl_port.c"
    "${UI}/ui.c" "${UI}/ui_ride.c" "${UI}/ui_led.c" "${UI}/ui_settings.c"
    "../app/measurement.c"
    "${COMMON}/src/up_lights.c"
    "${COMMON}/src/up_proto.c")
if(CONFIG_DASH_DEMO_CORE)
    list(APPEND srcs "../sim/sim_core.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "." "${UI}" "../app" "../sim" "${COMMON}/include"
    EMBED_FILES "${UI}/assets/Montserrat-Medium.ttf"
    PRIV_REQUIRES esp_psram spi_flash esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_ledc
                  esp_hw_support esp_mm esp_timer
)
