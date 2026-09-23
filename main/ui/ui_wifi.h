#pragma once
#include "ui_types.h"

// 内部接口：调用方持有 LVGL 锁。页面对象不向其它模块暴露可写句柄。
void ui_wifi_start_scan(void);
void ui_wifi_on_wifi_open(lv_event_t *e);
void ui_wifi_build(void);
lv_obj_t *ui_wifi_screen(ui_screen_t id);
void ui_wifi_destroy(void);
void ui_wifi_leave(void);
void ui_wifi_prepare_password(void);
