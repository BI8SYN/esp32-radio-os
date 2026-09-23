#pragma once
#include "ui_types.h"

// 内部接口：调用方持有 LVGL 锁。页面对象不向其它模块暴露可写句柄。
lv_obj_t *ui_visual_create(lv_obj_t *par, int width, int height);
void ui_visual_render(const int bands[PLAYER_SPECTRUM_BANDS], int level);
void ui_visual_destroy(void);
