#pragma once
#include "ui_types.h"

// 内部接口：调用方持有 LVGL 锁。页面对象不向其它模块暴露可写句柄。
void ui_stations_refresh_minibar(void);
void ui_stations_refresh_list(void);
void ui_stations_build(void);
void ui_stations_build_filter(void);
lv_obj_t *ui_stations_screen(ui_screen_t id);
void ui_stations_destroy(void);
void ui_stations_prepare_filter(void);
