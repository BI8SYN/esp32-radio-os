#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "stations.h"
#include "ota_mgr.h"
typedef enum { THEME_MODE_LIGHT, THEME_MODE_DARK, THEME_MODE_AUTO } theme_mode_t;
// 这里仅有应用数据，没有 LVGL 控件。初始化后由 UI 任务/持有 UI 锁的诊断入口访问。
// 页面可持有自己的控件和草稿，但当前电台、来源及偏好只有这一份。
typedef struct {
    // 已保存的外观、时间和显示偏好；theme_dark 也是离线开机的回退值。
    bool theme_dark;
    theme_mode_t theme_mode;
    int theme_accent;
    int theme_custom_hue;
    int dark_start_hour;
    int dark_end_hour;
    // 列表选择和播放会话跨页面共享，控件销毁不能清掉它们。
    station_src_t tab;
    station_filter_t filter;
    int catalog_page;
    station_t play_station;
    station_src_t play_source;
    bool has_station;
    bool wifi_connected;
    bool autoplay;
    int brightness;
    bool time_24h;
    // 临时状态不写 NVS，重启后不恢复尚未到期的睡眠计时。
    int sleep_minutes;
    int64_t sleep_deadline_us;
    bool screen_awake;
} app_radio_state_t;
app_radio_state_t *app_radio_state(void);
void app_radio_init(void);
void app_radio_network_changed(bool connected, ota_mgr_cb_t ota_callback);
esp_err_t app_radio_set_sleep(int seconds);
bool app_radio_tick(int64_t now_us);

int app_radio_source_count(station_src_t s);
const station_t *app_radio_source_get(station_src_t s, int i);
void app_radio_play_station(station_src_t src, int idx);
void app_radio_step_station(int dir);

// 左闭右开时段；相同开始/结束时刻表示全天暗色，与旧版本保持一致。
bool app_radio_hour_is_dark(int start, int end, int hour);
