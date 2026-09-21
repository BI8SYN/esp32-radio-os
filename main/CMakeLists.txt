idf_component_register(
    SRCS
        "main.c"
        "board.c"
        "stations.c"
        "player.c"
        "wifi_mgr.c"
        "ui.c"
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
