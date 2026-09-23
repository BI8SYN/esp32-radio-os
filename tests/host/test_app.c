// 编译真实 app_radio.c/preferences.c；仅替换硬件和 NVS 边界。
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_radio.h"
#include "preferences.h"
#include "player.h"
#include "nvs.h"

static station_t items[] = {{"A", "http://a", ""}, {"B", "http://b", ""}, {"C", "http://c", ""}};
uint32_t esp_random(void) { static uint32_t r = 123; r = r * 1664525u + 1013904223u; return r; }
static int plays, stops, checks, favorites = 0;
static esp_err_t ota_result = ESP_OK;
static int64_t now = 1000000;
static station_t last_play;
int64_t esp_timer_get_time(void) { return now; }
int stations_preset_count(void) { return 3; }
const station_t *stations_preset_get(int i) { return i >= 0 && i < 3 ? &items[i] : NULL; }
int stations_fav_count(void) { return favorites; }
const station_t *stations_fav_get(int i) { return i >= 0 && i < favorites ? &items[i] : NULL; }
int stations_catalog_filtered_count(const station_filter_t *f) { return f->region == 1 ? 1 : 3; }
const station_t *stations_catalog_filtered_get(const station_filter_t *f, int n, int *index)
{
    (void)index;
    return n >= 0 && n < stations_catalog_filtered_count(f) ? &items[f->region == 1 ? 1 : n] : NULL;
}
esp_err_t player_play(const station_t *s) { ++plays; last_play = *s; return ESP_OK; }
void player_stop(void) { ++stops; }
esp_err_t ota_mgr_check(ota_mgr_cb_t cb, void *data) { (void)cb; (void)data; ++checks; return ota_result; }

static struct { const char *key; int32_t value; } stored[20];
static int stored_count;
static station_t stored_station;
static size_t stored_size;
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h)
{
    (void)mode; assert(!strcmp(ns, "radio")); *h = 1; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
esp_err_t nvs_set_i32(nvs_handle_t h, const char *key, int32_t value)
{
    (void)h;
    for (int i = 0; i < stored_count; ++i) {
        if (!strcmp(key, stored[i].key)) { stored[i].value = value; return ESP_OK; }
    }
    assert(stored_count < 20);
    stored[stored_count].key = key; stored[stored_count++].value = value;
    return ESP_OK;
}
esp_err_t nvs_get_i32(nvs_handle_t h, const char *key, int32_t *value)
{
    (void)h;
    for (int i = 0; i < stored_count; ++i) {
        if (!strcmp(key, stored[i].key)) { *value = stored[i].value; return ESP_OK; }
    }
    return ESP_FAIL;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *p, size_t n)
{
    (void)h; assert(!strcmp(key, "last_sta")); assert(n == sizeof(stored_station));
    memcpy(&stored_station, p, n); stored_size = n; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *p, size_t *n)
{
    (void)h; assert(!strcmp(key, "last_sta"));
    if (!stored_size || *n < stored_size) return ESP_FAIL;
    memcpy(p, &stored_station, stored_size); *n = stored_size; return ESP_OK;
}

int main(void)
{
    for (int start = 0; start < 24; ++start) {
        for (int end = 0; end < 24; ++end) {
            int duration = (end - start + 24) % 24;
            for (int hour = 0; hour < 24; ++hour) {
                bool expected = duration == 0 || (hour - start + 24) % 24 < duration;
                assert(app_radio_hour_is_dark(start, end, hour) == expected);
            }
        }
    }
    app_radio_init();
    app_radio_state_t *s = app_radio_state();
    assert(s->brightness == 85 && s->theme_mode == THEME_MODE_AUTO && s->autoplay);
    assert(s->has_station && !strcmp(s->play_station.url, "http://a"));
    app_radio_play_station(STATION_SRC_PRESET, 2);
    app_radio_step_station(1); assert(!strcmp(last_play.url, "http://a"));
    app_radio_step_station(-1); assert(!strcmp(last_play.url, "http://c"));
    int before = plays; app_radio_play_station(STATION_SRC_PRESET, 99); assert(plays == before);
    s->play_source = STATION_SRC_FAV; app_radio_step_station(1);
    assert(s->play_source == STATION_SRC_PRESET); // 空收藏退回精选
    s->filter.region = 1; app_radio_play_station(STATION_SRC_CATALOG, 0);
    app_radio_step_station(1); assert(!strcmp(last_play.url, "http://b"));

    // 随机不立即换台；不重复当前台、上一台沿历史回退、单台不重连。
    s->filter.region = 0;
    app_radio_play_station(STATION_SRC_PRESET, 0);
    before = plays; app_radio_set_shuffle(true); assert(plays == before && s->shuffle);
    for (int i = 0; i < 50; ++i) {
        station_t previous = s->play_station;
        app_radio_step_station(1); assert(strcmp(previous.url, last_play.url));
        app_radio_step_station(-1); assert(!strcmp(previous.url, last_play.url));
    }
    before = plays; app_radio_step_station(-1); assert(plays == before);
    for (int i = 0; i < 30; ++i) app_radio_step_station(1);
    for (int i = 0; i < 12; ++i) app_radio_step_station(-1);
    before = plays; app_radio_step_station(-1); assert(plays == before);
    s->shuffle = false; preferences_load(); assert(s->shuffle); // 重启读取偏好
    s->filter.region = 1; app_radio_play_station(STATION_SRC_CATALOG, 0);
    s->filter.region = 0; // 浏览其他筛选不改变单台播放池
    before = plays; app_radio_step_station(1); assert(plays == before);
    favorites = 1; app_radio_play_station(STATION_SRC_FAV, 0);
    before = plays; app_radio_step_station(1); assert(plays == before);
    favorites = 0; app_radio_step_station(1);
    assert(s->play_source == STATION_SRC_PRESET && strcmp(last_play.url, "http://a"));
    app_radio_set_shuffle(false); assert(!s->shuffle);

    assert(app_radio_set_sleep(-1) == ESP_ERR_INVALID_ARG);
    assert(app_radio_set_sleep(5401) == ESP_ERR_INVALID_ARG);
    assert(app_radio_set_sleep(61) == ESP_OK && s->sleep_minutes == 2);
    assert(!app_radio_tick(now + 61000000 - 1));
    assert(app_radio_tick(now + 61000000) && stops == 1);
    assert(!app_radio_tick(now + 62000000) && stops == 1);
    app_radio_set_sleep(1); app_radio_set_sleep(0);
    assert(!app_radio_tick(now + 2000000));
    app_radio_set_sleep(1); now += 500000; app_radio_set_sleep(2);
    assert(!app_radio_tick(now + 1500000));
    assert(app_radio_tick(now + 2000000) && stops == 2);

    before = plays; ota_result = ESP_FAIL;
    app_radio_network_changed(false, NULL); assert(plays == before && checks == 0);
    app_radio_network_changed(true, NULL); assert(plays == before + 1 && checks == 1);
    ota_result = ESP_OK; app_radio_network_changed(true, NULL);
    app_radio_network_changed(false, NULL); app_radio_network_changed(true, NULL);
    assert(plays == before + 1 && checks == 2); // 一次续播，更新检查失败可重试

    // 使用旧固件真实键名写入，验证升级能读回；损坏/越界值不能覆盖安全默认值。
    nvs_set_i32(1, "bright", 37); nvs_set_i32(1, "thememode", 1);
    nvs_set_i32(1, "customhue", 188); nvs_set_i32(1, "darkstart", 20);
    nvs_set_i32(1, "darkend", 7); nvs_set_i32(1, "timefmt", 12);
    preferences_load();
    assert(s->brightness == 37 && s->theme_mode == THEME_MODE_DARK);
    assert(s->theme_custom_hue == 188 && s->dark_start_hour == 20 && s->dark_end_hour == 7 && !s->time_24h);
    nvs_set_i32(1, "bright", 999); nvs_set_i32(1, "thememode", 99);
    nvs_set_i32(1, "customhue", -1); preferences_load();
    assert(s->brightness == 37 && s->theme_mode == THEME_MODE_DARK && s->theme_custom_hue == 188);
    s->play_station = items[2]; s->play_source = STATION_SRC_CATALOG;
    preferences_save_last_station(); memset(&s->play_station, 0, sizeof(s->play_station));
    preferences_load(); assert(!strcmp(s->play_station.url, "http://c") && s->play_source == STATION_SRC_CATALOG);
    memset(&stored_station, 'x', sizeof(stored_station)); preferences_load();
    assert(s->play_station.name[STATION_NAME_MAX - 1] == 0 && s->play_station.url[STATION_URL_MAX - 1] == 0);
    puts("PASS application policy and legacy preference compatibility");
    return 0;
}
