idf_component_register(
    SRCS
        "main.c"
        "board.c"
        "stations.c"
        "player.c"
        "wifi_mgr.c"
        "ui.c"
        "app_radio.c"
        "preferences.c"
        "ui/ui_player.c"
        "ui/ui_stations.c"
        "ui/ui_settings.c"
        "ui/ui_wifi.c"
        "ui/ui_info.c"
        "ui/ui_visual.c"
        "ui/ui_boot.c"
        "ui/ui_widgets.c"
        "ui/ui_theme.c"
        "diagnostics.c"
        "ota_mgr.c"
        "assets/heart_icon.c"
        "assets/donation_qr.c"
        "fonts/font_cjk_14.c"
        "fonts/font_cjk_18.c"
        "fonts/font_cjk_24.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver esp_lcd esp_wifi esp_netif esp_event nvs_flash
        esp_http_client esp_http_server esp_https_ota esp_timer esp_adc app_update mbedtls json
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE RADIO_OTA_URL="${RADIO_OTA_URL}")

# 用真实 LVGL 输入/渲染链路做回归；默认关闭，不进入正式镜像。
option(RADIO_UI_TEST "Enable serial UI regression probes" OFF)
if(RADIO_UI_TEST)
    target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../tests/device/ui_probe.c")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../tests/device")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RADIO_UI_TEST=1)
endif()
