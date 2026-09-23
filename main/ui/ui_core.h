#pragma once
#include "ui_types.h"

// 内部接口：调用方持有 LVGL 锁。页面对象不向其它模块暴露可写句柄。
// 后台任务可调用：仅投递通知，不获取 LVGL 锁。
void ui_on_ota_status(const ota_mgr_status_t *status, void *user_data);
lv_obj_t *ui_page_screen(ui_screen_t id);
esp_err_t ui_navigate_locked(ui_screen_t screen);
void ui_back_to_settings(lv_event_t *event);
void ui_request_theme_rebuild(void);
void ui_boot_ready(void);

void ui_apply_pending_theme(void);
void ui_load_screen_locked(lv_obj_t *target);
