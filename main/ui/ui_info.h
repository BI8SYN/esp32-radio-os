#pragma once
#include "ui_types.h"

// 内部接口：调用方持有 LVGL 锁。页面对象不向其它模块暴露可写句柄。
void ui_info_release(lv_obj_t *screen);
void ui_info_on_about_open(lv_event_t *e);
void ui_info_on_diagnostics_open(lv_event_t *e);
void ui_info_build_about(void);
void ui_info_reset_touch_test(void);
void ui_info_build_touch_test(void);
void ui_info_build_diagnostics_screen(void);
void ui_info_refresh_diagnostics_screen(void);
void ui_info_refresh_ota(void);
void ui_info_on_ota_open(lv_event_t *e);
void ui_info_build_ota_screen(void);
lv_obj_t *ui_info_screen(ui_screen_t id);
void ui_info_destroy(void);
