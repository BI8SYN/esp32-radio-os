#pragma once
#include "ui_types.h"

// 内部接口：调用方持有 LVGL 锁。页面对象不向其它模块暴露可写句柄。
lv_color_t ui_theme_accent_color(int accent, int hue, bool dark);
lv_color_t ui_theme_tint_color(int accent, int hue, bool dark);
const char *ui_theme_accent_name(int accent);
bool ui_theme_desired_dark(void);
void ui_theme_apply_theme_choice(bool force_rebuild);
void ui_theme_set_theme_mode_locked(theme_mode_t mode);
void ui_theme_set_dark_schedule_locked(int start_hour, int end_hour);
