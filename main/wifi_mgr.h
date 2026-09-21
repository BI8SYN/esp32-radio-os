// Wi-Fi 管理：NVS 里有凭据就直接连；没有就开配网热点，
// 手机连上后在网页里填账号密码（屏幕会显示引导）。
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SETUP_AP_SSID "ES3C28P-Radio"
#define WIFI_SETUP_IP      "192.168.4.1"

#define WIFI_MGR_SCAN_MAX 20

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secured;
} wifi_mgr_ap_t;

esp_err_t wifi_mgr_init(void);
bool wifi_mgr_is_connected(void);
int wifi_mgr_rssi(void);  // 未连接时返回 -127 dBm

// 本机屏幕配网使用：扫描是阻塞操作，应从工作任务调用。
esp_err_t wifi_mgr_scan(wifi_mgr_ap_t *items, size_t capacity, size_t *count);
esp_err_t wifi_mgr_connect(const char *ssid, const char *password);
void wifi_mgr_current_ssid(char *out, size_t out_len);

// 主动进入配网模式（界面上的「重新配网」用）。不清除已保存的凭据，
// 用户在网页里提交新的才会覆盖。
esp_err_t wifi_mgr_start_setup(void);

// 清掉已保存的凭据（按住 BOOT 上电时用）。
esp_err_t wifi_mgr_reset(void);

#ifdef __cplusplus
}
#endif
