#pragma once
#include "ui_types.h"

// 内部接口：调用方持有 LVGL 锁。页面对象不向其它模块暴露可写句柄。
void ui_player_refresh_wifi(void);
void ui_player_refresh_ota_banner(void);
void ui_player_refresh(void);
void ui_player_on_goto_player(lv_event_t *e);
void ui_player_set_display_awake_locked(bool awake);
void ui_player_update_sleep_label(void);
void ui_player_update_sleep_countdown(void);
void ui_player_build_screen_wake_layer(void);
void ui_player_build(void);
lv_obj_t *ui_player_screen(ui_screen_t id);
void ui_player_destroy(void);
void ui_player_tick(unsigned ticks);
