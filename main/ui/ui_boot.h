#pragma once
#include "ui_types.h"

// 内部接口：调用方持有 LVGL 锁。页面对象不向其它模块暴露可写句柄。
void ui_boot_build(void);
lv_obj_t *ui_boot_screen(ui_screen_t id);
void ui_boot_start_timer(void);
