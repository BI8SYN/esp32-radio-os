#include "preferences.h"
#include "app_radio.h"
#include "nvs.h"

void preferences_save_tab(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, NVS_KEY_TAB, (int32_t)app_radio_state()->tab);
        nvs_commit(h);
        nvs_close(h);
    }
}

void preferences_save_i32(const char *key, int32_t value)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, key, value);
        nvs_commit(h);
        nvs_close(h);
    }
}

void preferences_save_last_station(void)
{
    if (!app_radio_state()->has_station) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY_LAST, &app_radio_state()->play_station, sizeof(app_radio_state()->play_station));
        nvs_set_i32(h, NVS_KEY_LASTSRC, (int32_t)app_radio_state()->play_source);
        nvs_commit(h);
        nvs_close(h);
    }
}

void preferences_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        int32_t t = STATION_SRC_PRESET;
        if (nvs_get_i32(h, NVS_KEY_TAB, &t) == ESP_OK && t >= 0 && t <= 2) {
            app_radio_state()->tab = (station_src_t)t;
        }
        int32_t value;
        if (nvs_get_i32(h, NVS_KEY_SHUFFLE, &value) == ESP_OK) app_radio_state()->shuffle = value == 1;
        if (nvs_get_i32(h, NVS_KEY_AUTO, &value) == ESP_OK) app_radio_state()->autoplay = value != 0;
        if (nvs_get_i32(h, NVS_KEY_BRIGHT, &value) == ESP_OK && value >= 10 && value <= 100) {
            app_radio_state()->brightness = value;
        }
        if (nvs_get_i32(h, NVS_KEY_TIMEFMT, &value) == ESP_OK) {
            app_radio_state()->time_24h = value != 12;
        }
        if (nvs_get_i32(h, NVS_KEY_THEME_MODE, &value) == ESP_OK &&
            value >= THEME_MODE_LIGHT && value <= THEME_MODE_AUTO) {
            app_radio_state()->theme_mode = (theme_mode_t)value;
        }
        if (nvs_get_i32(h, NVS_KEY_THEME_ACCENT, &value) == ESP_OK && value >= 0 && value <= 4) {
            app_radio_state()->theme_accent = value;
        }
        if (nvs_get_i32(h, NVS_KEY_CUSTOM_HUE, &value) == ESP_OK && value >= 0 && value <= 359) {
            app_radio_state()->theme_custom_hue = value;
        }
        if (nvs_get_i32(h, NVS_KEY_DARK_START, &value) == ESP_OK && value >= 0 && value < 24) {
            app_radio_state()->dark_start_hour = value;
        }
        if (nvs_get_i32(h, NVS_KEY_DARK_END, &value) == ESP_OK && value >= 0 && value < 24) {
            app_radio_state()->dark_end_hour = value;
        }
        if (nvs_get_i32(h, NVS_KEY_DARK_LAST, &value) == ESP_OK) {
            app_radio_state()->theme_dark = value != 0;
        }
        size_t last_len = sizeof(app_radio_state()->play_station);
        if (nvs_get_blob(h, NVS_KEY_LAST, &app_radio_state()->play_station, &last_len) == ESP_OK &&
            last_len == sizeof(app_radio_state()->play_station) && app_radio_state()->play_station.name[0] && app_radio_state()->play_station.url[0]) {
            app_radio_state()->play_station.name[sizeof(app_radio_state()->play_station.name) - 1] = '\0';
            app_radio_state()->play_station.url[sizeof(app_radio_state()->play_station.url) - 1] = '\0';
            app_radio_state()->play_station.meta[sizeof(app_radio_state()->play_station.meta) - 1] = '\0';
            app_radio_state()->has_station = true;
            int32_t src = STATION_SRC_PRESET;
            if (nvs_get_i32(h, NVS_KEY_LASTSRC, &src) == ESP_OK && src >= 0 && src <= 2) {
                app_radio_state()->play_source = (station_src_t)src;
            }
        }
        nvs_close(h);
    }

}
