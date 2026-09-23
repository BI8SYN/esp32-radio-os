#pragma once
// UI 内部协议。仅在持有 LVGL 锁时访问页面或 app_radio_state()；后台任务只投递通知。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui.h"
#include "app_radio.h"
#include "preferences.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ota_mgr.h"
#include "player.h"
#include "stations.h"
#include "wifi_mgr.h"
LV_FONT_DECLARE(font_cjk_14);
LV_FONT_DECLARE(font_cjk_18);
LV_FONT_DECLARE(font_cjk_24);
typedef enum { SMALL_ICON_DISPLAY_OFF, SMALL_ICON_TIMER } small_icon_t;
#define ROW_H 45
#define HEADER_H 42
#define MINIBAR_H 36
#define BOOT_HOLD_MS 2200
#define BOOT_FADE_MS 320
#define CATALOG_PAGE_SIZE 16
#define C_PAPER   lv_color_hex(app_radio_state()->theme_dark ? 0x101116 : 0xF8F8F5)
#define C_SURFACE lv_color_hex(app_radio_state()->theme_dark ? 0x191A22 : 0xEEEEF2)
#define C_RAISED  lv_color_hex(app_radio_state()->theme_dark ? 0x252631 : 0xE4E4EA)
#define C_INK     lv_color_hex(app_radio_state()->theme_dark ? 0xF3F4F7 : 0x17171C)
#define C_INK2    lv_color_hex(app_radio_state()->theme_dark ? 0xA7A9B3 : 0x696970)
#define C_LINE    lv_color_hex(app_radio_state()->theme_dark ? 0x323440 : 0xD9D9DF)
#define C_ACCENT  ui_theme_accent_color(app_radio_state()->theme_accent, app_radio_state()->theme_custom_hue, app_radio_state()->theme_dark)
#define C_TINT    ui_theme_tint_color(app_radio_state()->theme_accent, app_radio_state()->theme_custom_hue, app_radio_state()->theme_dark)
#define C_WHITE   lv_color_hex(0xFFFFFF)
#define C_ON_ACCENT (lv_color_brightness(C_ACCENT) > 160 ? lv_color_hex(0x17171C) : C_WHITE)
