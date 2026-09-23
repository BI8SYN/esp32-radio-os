#pragma once
#include <stdint.h>
// 保留原命名空间、键名和数据格式，重构后无需清除已保存的设置。
void preferences_load(void);

#define NVS_NS     "radio"
#define NVS_KEY_TAB "tab"
#define NVS_KEY_AUTO "autoplay"
#define NVS_KEY_BRIGHT "bright"
#define NVS_KEY_LAST "last_sta"
#define NVS_KEY_LASTSRC "last_src"
#define NVS_KEY_TIMEFMT "timefmt"
#define NVS_KEY_THEME_MODE "thememode"
#define NVS_KEY_THEME_ACCENT "accent"
#define NVS_KEY_CUSTOM_HUE "customhue"
#define NVS_KEY_DARK_START "darkstart"
#define NVS_KEY_DARK_END "darkend"
#define NVS_KEY_DARK_LAST "darklast"

void preferences_save_tab(void);
void preferences_save_i32(const char *key, int32_t value);
void preferences_save_last_station(void);
