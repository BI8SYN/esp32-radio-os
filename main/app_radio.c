#include "app_radio.h"
#include "preferences.h"
#include "player.h"
#include "esp_timer.h"
#include <string.h>

static int src_index_of(station_src_t s, const char *url);

int app_radio_source_count(station_src_t s)
{
    switch (s) {
    case STATION_SRC_FAV:    return stations_fav_count();
    case STATION_SRC_PRESET: return stations_preset_count();
    default:                 return stations_catalog_filtered_count(&app_radio_state()->filter);
    }
}

const station_t *app_radio_source_get(station_src_t s, int i)
{
    switch (s) {
    case STATION_SRC_FAV:    return stations_fav_get(i);
    case STATION_SRC_PRESET: return stations_preset_get(i);
    default:                 return stations_catalog_filtered_get(&app_radio_state()->filter, i, NULL);
    }
}

static int src_index_of(station_src_t s, const char *url)
{
    int n = app_radio_source_count(s);
    for (int i = 0; i < n; i++) {
        const station_t *st = app_radio_source_get(s, i);
        if (st && strcmp(st->url, url) == 0) return i;
    }
    return -1;
}

void app_radio_play_station(station_src_t src, int idx)
{
    const station_t *st = app_radio_source_get(src, idx);
    if (!st) return;
    app_radio_state()->play_station = *st;
    app_radio_state()->play_source = src;
    app_radio_state()->has_station = true;
    preferences_save_last_station();
    player_play(&app_radio_state()->play_station);
}

void app_radio_step_station(int dir)
{
    if (!app_radio_state()->has_station) return;
    int n = app_radio_source_count(app_radio_state()->play_source);
    if (n <= 0) {
        app_radio_state()->play_source = STATION_SRC_PRESET;
        n = app_radio_source_count(app_radio_state()->play_source);
        if (n <= 0) return;
    }
    int cur = src_index_of(app_radio_state()->play_source, app_radio_state()->play_station.url);
    if (cur < 0) cur = 0;
    int next = ((cur + dir) % n + n) % n;
    app_radio_play_station(app_radio_state()->play_source, next);
}

static app_radio_state_t s_state = {
    .theme_mode = THEME_MODE_AUTO, .theme_custom_hue = 260,
    .dark_start_hour = 22, .dark_end_hour = 8,
    .tab = STATION_SRC_PRESET, .play_source = STATION_SRC_PRESET,
    .autoplay = true, .brightness = 85, .time_24h = true, .screen_awake = true,
};
static bool s_autoplay_done, s_ota_auto_checked;
app_radio_state_t *app_radio_state(void) { return &s_state; }
void app_radio_init(void)
{
    preferences_load();
    if (!s_state.has_station && stations_preset_count() > 0) {
        s_state.play_station = *stations_preset_get(0);
        s_state.play_source = STATION_SRC_PRESET;
        s_state.has_station = true;
    }
}
void app_radio_network_changed(bool connected, ota_mgr_cb_t ota_callback)
{
    s_state.wifi_connected = connected;
    if (connected && s_state.autoplay && !s_autoplay_done && s_state.has_station) {
        s_autoplay_done = true;
        player_play(&s_state.play_station);
    }
    if (connected && !s_ota_auto_checked) {
        if (ota_mgr_check(ota_callback, NULL) == ESP_OK) s_ota_auto_checked = true;
    }
}
esp_err_t app_radio_set_sleep(int seconds)
{
    if (seconds < 0 || seconds > 90 * 60) return ESP_ERR_INVALID_ARG;
    s_state.sleep_minutes = seconds > 0 ? (seconds + 59) / 60 : 0;
    s_state.sleep_deadline_us = seconds > 0 ? esp_timer_get_time() + (int64_t)seconds * 1000000 : 0;
    return ESP_OK;
}
// 与界面 tick 共用单调时钟，不再让 FreeRTOS 定时器跨任务修改应用状态。
// 返回 true 表示刚到期：停播由应用层处理，息屏和标签刷新交给界面层。
bool app_radio_tick(int64_t now_us)
{
    if (!s_state.sleep_deadline_us || now_us < s_state.sleep_deadline_us) return false;
    s_state.sleep_deadline_us = 0;
    s_state.sleep_minutes = 0;
    player_stop();
    return true;
}

bool app_radio_hour_is_dark(int start, int end, int hour)
{
    if (start == end) return true;
    if (start < end) return hour >= start && hour < end;
    return hour >= start || hour < end;
}
