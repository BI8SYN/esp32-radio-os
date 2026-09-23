// LVGL 产品界面：亮色 / 暗色 / 自动外观、四组主题色与启动动效。
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 需在 board_display_init() 之后调用。
esp_err_t ui_init(void);

typedef enum {
    UI_SCREEN_PLAYER = 0,
    UI_SCREEN_STATIONS,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_FILTER,
    UI_SCREEN_WIFI,
    UI_SCREEN_WIFI_PASSWORD,
    UI_SCREEN_ABOUT,
    UI_SCREEN_OTA,
    UI_SCREEN_DIAGNOSTICS,
    UI_SCREEN_TOUCH_TEST,
    // 以下仅用于内部页面识别，ui_show_screen() 不接受直接跳转。
    UI_SCREEN_SETUP,
    UI_SCREEN_BOOT,
} ui_screen_t;

// 物理 USB 串口诊断使用；也让产测可以不依赖触摸完成完整操作。
esp_err_t ui_show_screen(ui_screen_t screen);
esp_err_t ui_set_drawer_visible(bool visible);
esp_err_t ui_set_donation_visible(bool visible);
esp_err_t ui_set_display_awake(bool awake);
esp_err_t ui_set_sleep_timer_seconds(int seconds);
esp_err_t ui_set_theme_mode(int mode);       // 0=亮色，1=暗色，2=自动
esp_err_t ui_set_theme_accent(int accent);   // 0=紫，1=青，2=橙，3=玫红
esp_err_t ui_set_theme_custom_hue(int hue);  // 0–359，自定义色相
esp_err_t ui_set_dark_schedule(int start_hour, int end_hour);
esp_err_t ui_show_appearance_settings(void);
esp_err_t ui_show_theme_picker(void);
const char *ui_theme_mode_name(void);
bool ui_theme_is_dark(void);
int ui_theme_accent(void);
int ui_theme_custom_hue(void);
int ui_dark_start_hour(void);
int ui_dark_end_hour(void);
int ui_settings_scroll_y(void);
esp_err_t ui_play_catalog_index(int index);
esp_err_t ui_set_catalog_filter(int region, int category);
esp_err_t ui_step_station(int direction);

// 产测健康探针：确认 LVGL 任务仍能释放界面锁。
bool ui_responsive(void);

// 网络状态变化时通知界面（更新 Wi-Fi 图标）。可从任意任务调用。
void ui_set_wifi_connected(bool connected);

// 配网引导页：屏幕上显示热点名和配置地址。
// failed_ssid 非 NULL 表示这次是「连不上已保存的网络」才进的配网，会在页面上说明。
void ui_show_setup(const char *ap_ssid, const char *ip, const char *failed_ssid);

// 用户在网页里提交了凭据，屏幕转成「正在连接 xxx…」
void ui_show_setup_connecting(const char *ssid);

// 连上网后回到播放页。
void ui_hide_setup(void);

#ifdef __cplusplus
}
#endif
