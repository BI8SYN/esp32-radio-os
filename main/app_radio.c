#include "app_radio.h"
#include "preferences.h"
#include "player.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <string.h>

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

// 历史只属于当前播放会话，不写入闪存。保存台的副本，收藏删除或列表刷新
// 不会使“上一台”引用失效；固定容量避免长时间收听时内存持续增长。
#define HISTORY_CAPACITY 12
static station_t s_history[HISTORY_CAPACITY];
static unsigned s_history_count;
static station_filter_t s_play_filter;

static void play_selected(const station_t *station)
{
    app_radio_state()->play_station = *station;
    app_radio_state()->has_station = true;
    preferences_save_last_station();
    player_play(&app_radio_state()->play_station);
}

void app_radio_play_station(station_src_t src, int idx)
{
    const station_t *st = app_radio_source_get(src, idx);
    if (!st) return;
    s_history_count = 0;
    s_play_filter = app_radio_state()->filter;
    app_radio_state()->play_source = src;
    play_selected(st);
}

void app_radio_set_shuffle(bool enabled)
{
    if (app_radio_state()->shuffle == enabled) return;
    app_radio_state()->shuffle = enabled;
    s_history_count = 0;
    preferences_save_i32(NVS_KEY_SHUFFLE, enabled);
}

// 随机范围以选台时的筛选为准，浏览/筛选别的列表不会悄悄改变播放来源。
static const station_t *play_pool_get(int i)
{
    if (app_radio_state()->play_source == STATION_SRC_CATALOG)
        return stations_catalog_filtered_get(&s_play_filter, i, NULL);
    return app_radio_source_get(app_radio_state()->play_source, i);
}

void app_radio_step_station(int dir)
{
    app_radio_state_t *s = app_radio_state();
    if (!s->has_station) return;
    if (s->shuffle && dir < 0) {
        if (s_history_count) play_selected(&s_history[--s_history_count]);
        return;
    }
    int n = s->play_source == STATION_SRC_CATALOG
        ? stations_catalog_filtered_count(&s_play_filter) : app_radio_source_count(s->play_source);
    if (n <= 0) {
        s->play_source = STATION_SRC_PRESET;
        s_history_count = 0;
        n = stations_preset_count();
        if (n <= 0) return;
    }
    int cur = -1;
    for (int i = 0; i < n; ++i) {
        const station_t *st = play_pool_get(i);
        if (st && !strcmp(st->url, s->play_station.url)) { cur = i; break; }
    }
    const station_t *next = NULL;
    if (s->shuffle) {
        // 蓄水池抽样：不分配候选数组，跳过同一流地址（包括重复条目）。
        unsigned candidates = 0;
        for (int i = 0; i < n; ++i) {
            const station_t *st = play_pool_get(i);
            if (!st || !strcmp(st->url, s->play_station.url)) continue;
            if (esp_random() % ++candidates == 0) next = st;
        }
        if (!next) return; // 只有当前台时不重连、不打断播放。
        if (s_history_count == HISTORY_CAPACITY) {
            memmove(s_history, s_history + 1, sizeof(s_history[0]) * (HISTORY_CAPACITY - 1));
            --s_history_count;
        }
        s_history[s_history_count++] = s->play_station;
    } else {
        int index = cur < 0 ? (dir < 0 ? n - 1 : 0) : ((cur + dir) % n + n) % n;
        next = play_pool_get(index);
    }
    if (next) play_selected(next);
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
