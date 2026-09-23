#pragma once
#include "ui_types.h"

// 内部接口：调用方持有 LVGL 锁。页面对象不向其它模块暴露可写句柄。
void ui_settings_build(void);
void ui_settings_on_open_settings(lv_event_t *e);
lv_obj_t *ui_settings_screen(ui_screen_t id);
void ui_settings_destroy(void);
void ui_settings_network_changed(void);
int ui_settings_scroll_locked(void);
void ui_settings_restore_scroll(int y);
