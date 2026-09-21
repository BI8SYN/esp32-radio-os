#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "assets/heart_icon.h"
#include "assets/donation_qr.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lvgl.h"
#include "nvs.h"
#include "ota_mgr.h"
#include "player.h"
#include "stations.h"
#include "wifi_mgr.h"

static const char *TAG = "ui";

LV_FONT_DECLARE(font_cjk_14);
LV_FONT_DECLARE(font_cjk_18);
LV_FONT_DECLARE(font_cjk_24);

typedef enum {
    THEME_MODE_LIGHT = 0,
    THEME_MODE_DARK,
    THEME_MODE_AUTO,
} theme_mode_t;

static bool s_theme_dark;
static theme_mode_t s_theme_mode = THEME_MODE_AUTO;
static int s_theme_accent;
static int s_theme_custom_hue = 260;
static int s_dark_start_hour = 22;
static int s_dark_end_hour = 8;
static bool s_theme_rebuild_scheduled;
static bool s_theme_rebuild_pending;
static bool s_boot_ready;

// 紫罗兰、青绿、暖橙、玫红四组差异明确的预置色。QD2833 IPS 始终由 board.c 开启 INVON；
// 主题只改变语义色，绝不能通过交换黑白来补偿硬件极性。
static const uint32_t THEME_ACCENT_LIGHT[] = {0x635BFF, 0x007F73, 0xB85713, 0xC2386A};
static const uint32_t THEME_ACCENT_DARK[]  = {0x9A94FF, 0x3CCDB9, 0xFF9C5A, 0xFF7BA8};
static const uint32_t THEME_TINT_LIGHT[]   = {0xEEECFF, 0xE6F6F3, 0xFFF0E5, 0xFFEAF1};
static const uint32_t THEME_TINT_DARK[]    = {0x2B284F, 0x153B35, 0x452B1C, 0x472333};
static const char *THEME_ACCENT_NAMES[] = {"紫罗兰", "青绿色", "暖橙色", "玫红色", "自定义"};

static lv_color_t theme_accent_color_for(int accent, int hue, bool dark);
static lv_color_t theme_tint_color_for(int accent, int hue, bool dark);

#define C_PAPER   lv_color_hex(s_theme_dark ? 0x101116 : 0xF8F8F5)
#define C_SURFACE lv_color_hex(s_theme_dark ? 0x191A22 : 0xEEEEF2)
#define C_RAISED  lv_color_hex(s_theme_dark ? 0x252631 : 0xE4E4EA)
#define C_INK     lv_color_hex(s_theme_dark ? 0xF3F4F7 : 0x17171C)
#define C_INK2    lv_color_hex(s_theme_dark ? 0xA7A9B3 : 0x696970)
#define C_LINE    lv_color_hex(s_theme_dark ? 0x323440 : 0xD9D9DF)
#define C_ACCENT  theme_accent_color_for(s_theme_accent, s_theme_custom_hue, s_theme_dark)
#define C_TINT    theme_tint_color_for(s_theme_accent, s_theme_custom_hue, s_theme_dark)
#define C_WHITE   lv_color_hex(0xFFFFFF)
#define C_ON_ACCENT (lv_color_brightness(C_ACCENT) > 160 ? lv_color_hex(0x17171C) : C_WHITE)

#define ROW_H      45
#define HEADER_H   42
#define MINIBAR_H  36
#define BOOT_HOLD_MS 2200
#define BOOT_FADE_MS 320
#define CATALOG_PAGE_SIZE 16
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

// ------------------------------------------------------------------- 状态
static lv_obj_t *scr_boot, *scr_player, *scr_list, *scr_filter, *scr_setup, *scr_settings;
static lv_obj_t *scr_wifi, *scr_wifi_pass;
static lv_obj_t *scr_about, *scr_ota, *scr_diagnostics, *scr_touch_test;
static lv_obj_t *lbl_net_state;
static lv_obj_t *wifi_content, *wifi_ssid_input, *wifi_password, *wifi_keyboard;
static lv_obj_t *settings_body, *settings_auto, *settings_brightness, *settings_time_btn[2];
static lv_obj_t *settings_theme_btn[3], *settings_accent_dot;
static lv_obj_t *settings_schedule_row, *settings_dark_start, *settings_dark_end;
static lv_obj_t *theme_picker_layer, *theme_picker_wheel, *theme_picker_preview;
static lv_obj_t *theme_picker_value, *theme_picker_preset_btn[4];
static int s_picker_pending_accent;
static int s_picker_pending_hue;
static bool s_picker_syncing;

// 播放页
static lv_obj_t *lbl_name, *lbl_meta, *lbl_state, *btn_player_play, *btn_player_fav;
static lv_obj_t *home_update_banner, *home_update_label;
static lv_obj_t *row_err, *lbl_wifi, *lbl_offline, *lbl_vol_low, *lbl_vol_high, *sld_vol;
static lv_obj_t *lbl_clock, *top_sleep_group, *lbl_sleep_countdown;
static lv_obj_t *player_controls, *home_visual;
static lv_obj_t *drawer_layer, *drawer_sleep_label;
static lv_obj_t *donation_layer;
static lv_obj_t *screen_wake_layer;
static lv_obj_t *ota_body, *diagnostics_body;
static lv_obj_t *touch_target, *touch_dot, *touch_status, *touch_detail, *touch_restart;
static lv_obj_t *visual_canvas;
static lv_color_t *visual_buffer;
static unsigned s_live_ticks;
static int32_t s_play_morph;
static bool s_play_icon_active;
// 中心一根 + 向外八级的显示域平滑值；绘制时左右镜像。
static int s_visual_levels[PLAYER_SPECTRUM_BANDS];
static int s_boot_wave;
static int s_touch_step;
static int s_touch_dx[5], s_touch_dy[5];

static const lv_point_t s_touch_targets[5] = {
    {28, 92}, {292, 92}, {160, 145}, {28, 215}, {292, 215},
};

// 列表页
static lv_obj_t *list_content, *btn_filter, *lbl_filter, *lbl_title;
static lv_obj_t *tab_btn[3], *tab_lbl[3];
static lv_obj_t *mini_name, *mini_state, *mini_icon;

// 筛选页
static lv_obj_t **chip_cat, *region_dropdown;

static station_src_t s_tab = STATION_SRC_PRESET;
static station_filter_t s_filter = { 0, 0 };
static station_filter_t s_draft;
static int s_catalog_page;

// 当前播放的电台，以及它来自哪个列表（决定上一台/下一台的循环范围）
static station_t s_play_st;
static station_src_t s_play_src = STATION_SRC_PRESET;
static bool s_has_station;
static bool s_wifi_ok;
static bool s_autoplay = true;
static bool s_ota_auto_checked;
static bool s_autoplay_done;
static int s_brightness = 85;
static bool s_time_24h = true;
static int s_sleep_minutes;
static TimerHandle_t s_sleep_timer;
static volatile int64_t s_sleep_deadline_us;
static volatile bool s_sleep_expired;
static bool s_screen_awake = true;
static wifi_mgr_ap_t s_wifi_items[WIFI_MGR_SCAN_MAX];
static size_t s_wifi_count;
static int s_wifi_selected = -1;
static bool s_wifi_scanning;
static bool s_wifi_visible;

static void request_theme_rebuild(void);
static void boot_transition_done_cb(lv_timer_t *timer);

// ------------------------------------------------------------- 列表数据访问
static int src_count(station_src_t s)
{
    switch (s) {
    case STATION_SRC_FAV:    return stations_fav_count();
    case STATION_SRC_PRESET: return stations_preset_count();
    default:                 return stations_catalog_filtered_count(&s_filter);
    }
}

static const station_t *src_get(station_src_t s, int i)
{
    switch (s) {
    case STATION_SRC_FAV:    return stations_fav_get(i);
    case STATION_SRC_PRESET: return stations_preset_get(i);
    default:                 return stations_catalog_filtered_get(&s_filter, i, NULL);
    }
}

static int src_index_of(station_src_t s, const char *url)
{
    int n = src_count(s);
    for (int i = 0; i < n; i++) {
        const station_t *st = src_get(s, i);
        if (st && strcmp(st->url, url) == 0) return i;
    }
    return -1;
}

static int filter_active_count(const station_filter_t *f)
{
    return (f->cat != 0) + (f->region != 0);
}

static void save_tab(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, NVS_KEY_TAB, (int32_t)s_tab);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void save_i32(const char *key, int32_t value)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, key, value);
        nvs_commit(h);
        nvs_close(h);
    }
}

static lv_color_t custom_accent_color(int hue, bool dark)
{
    if (hue < 0) hue = 0;
    if (hue > 359) hue = 359;
    if (dark) return lv_color_hsv_to_rgb((uint16_t)hue, 58, 100);

    // 从饱和而不过亮的颜色开始；黄色、青色等高亮色自动压暗，保证白字可读。
    uint8_t value = 82;
    lv_color_t color = lv_color_hsv_to_rgb((uint16_t)hue, 72, value);
    while (lv_color_brightness(color) > 132 && value > 48) {
        value -= 3;
        color = lv_color_hsv_to_rgb((uint16_t)hue, 72, value);
    }
    return color;
}

static lv_color_t theme_accent_color_for(int accent, int hue, bool dark)
{
    if (accent >= 0 && accent < 4) {
        return lv_color_hex(dark ? THEME_ACCENT_DARK[accent] : THEME_ACCENT_LIGHT[accent]);
    }
    return custom_accent_color(hue, dark);
}

static lv_color_t theme_tint_color_for(int accent, int hue, bool dark)
{
    if (accent >= 0 && accent < 4) {
        return lv_color_hex(dark ? THEME_TINT_DARK[accent] : THEME_TINT_LIGHT[accent]);
    }
    return lv_color_hsv_to_rgb((uint16_t)hue, dark ? 45 : 18, dark ? 28 : 100);
}

static const char *theme_accent_name(int accent)
{
    if (accent < 0 || accent > 4) accent = 0;
    return THEME_ACCENT_NAMES[accent];
}

static bool scheduled_dark_at_hour(int hour)
{
    if (s_dark_start_hour == s_dark_end_hour) return true;
    if (s_dark_start_hour < s_dark_end_hour) {
        return hour >= s_dark_start_hour && hour < s_dark_end_hour;
    }
    return hour >= s_dark_start_hour || hour < s_dark_end_hour;
}

static bool theme_desired_dark(void)
{
    if (s_theme_mode == THEME_MODE_LIGHT) return false;
    if (s_theme_mode == THEME_MODE_DARK) return true;
    time_t now = time(NULL);
    struct tm local;
    if (now > 1700000000 && localtime_r(&now, &local)) {
        return scheduled_dark_at_hour(local.tm_hour);
    }
    // 首次联网校时前沿用上次自动判断，避免夜间重启先闪亮色。
    return s_theme_dark;
}

static void apply_theme_choice(bool force_rebuild)
{
    bool dark = theme_desired_dark();
    bool changed = dark != s_theme_dark;
    if (changed) {
        s_theme_dark = dark;
        save_i32(NVS_KEY_DARK_LAST, dark ? 1 : 0);
    }
    if (changed || force_rebuild) request_theme_rebuild();
}

static void set_theme_mode_locked(theme_mode_t mode)
{
    bool mode_changed = mode != s_theme_mode;
    s_theme_mode = mode;
    if (mode_changed) save_i32(NVS_KEY_THEME_MODE, mode);

    if (mode == THEME_MODE_AUTO) {
        // 用户主动选择“自动”时立即根据当前时间重算，
        // 并强制重绘一次，避免缓存状态与已绘制页面短暂不一致。
        apply_theme_choice(true);
        return;
    }

    bool dark = mode == THEME_MODE_DARK;
    if (dark != s_theme_dark) {
        s_theme_dark = dark;
        save_i32(NVS_KEY_DARK_LAST, dark ? 1 : 0);
    }
    if (mode_changed) request_theme_rebuild();
}

static void set_dark_schedule_locked(int start_hour, int end_hour)
{
    bool changed = start_hour != s_dark_start_hour || end_hour != s_dark_end_hour;
    s_dark_start_hour = start_hour;
    s_dark_end_hour = end_hour;
    if (changed) {
        save_i32(NVS_KEY_DARK_START, start_hour);
        save_i32(NVS_KEY_DARK_END, end_hour);
    }
    // 修改时段属于用户主动操作；自动模式下必须当场重算与刷新。
    if (s_theme_mode == THEME_MODE_AUTO) apply_theme_choice(true);
}

static void save_last_station(void)
{
    if (!s_has_station) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY_LAST, &s_play_st, sizeof(s_play_st));
        nvs_set_i32(h, NVS_KEY_LASTSRC, (int32_t)s_play_src);
        nvs_commit(h);
        nvs_close(h);
    }
}

// ------------------------------------------------------------- 通用小工具
static void plain(lv_obj_t *o)
{
    lv_obj_set_style_bg_color(o, C_PAPER, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *mk_box(lv_obj_t *par, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *o = lv_obj_create(par);
    plain(o);
    lv_obj_set_size(o, w, h);
    return o;
}

static lv_obj_t *mk_label(lv_obj_t *par, const char *txt, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

static lv_obj_t *mk_btn(lv_obj_t *par, const char *txt, const lv_font_t *f, bool primary,
                        lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_btn_create(par);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, primary ? C_ACCENT : C_SURFACE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_bg_color(b, primary ? lv_color_darken(C_ACCENT, LV_OPA_20) : C_RAISED,
                              LV_STATE_PRESSED);
    lv_obj_t *l = mk_label(b, txt, f, primary ? C_ON_ACCENT : C_INK);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

// SVG 源轮廓在构建时转成 24x24 A8 蒙版。空心/实心共享同一轮廓，既有
// 抗锯齿，也不依赖字体或 LVGL 8 不支持的凹多边形填充。
static void draw_favorite_heart(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const lv_coord_t cx = (area.x1 + area.x2) / 2;
    const lv_coord_t cy = (area.y1 + area.y2) / 2;
    bool checked = (lv_obj_get_state(obj) & LV_STATE_CHECKED) != 0;
    lv_draw_img_dsc_t icon;
    lv_draw_img_dsc_init(&icon);
    icon.recolor = checked ? C_ACCENT : C_INK2;
    icon.recolor_opa = LV_OPA_COVER;
    icon.opa = LV_OPA_COVER;
    // 轮廓本身略偏下，绘制区域向上做 1px 光学修正。
    lv_area_t icon_area = {cx - 12, cy - 13, cx + 11, cy + 10};
    lv_draw_img(ctx, &icon, &icon_area,
                checked ? &heart_icon_filled : &heart_icon_outline);
}

typedef enum {
    PLAYER_ICON_LIST,
    PLAYER_ICON_PREV,
    PLAYER_ICON_PLAY,
    PLAYER_ICON_NEXT,
} player_icon_t;

static lv_coord_t morph_coord(lv_coord_t from, lv_coord_t to)
{
    return from + (to - from) * s_play_morph / 100;
}

static void draw_player_icon(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    player_icon_t kind = (player_icon_t)(intptr_t)lv_event_get_user_data(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const lv_coord_t cx = (area.x1 + area.x2) / 2;
    const lv_coord_t cy = (area.y1 + area.y2) / 2;

    if (kind == PLAYER_ICON_LIST) {
        static const int8_t widths[] = {14, 18, 11};
        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.width = 2;
        line.round_start = true;
        line.round_end = true;
        line.color = C_INK;
        for (int i = 0; i < 3; ++i) {
            lv_coord_t y = cy - 6 + i * 6;
            lv_point_t points[2] = {
                {cx - 9, y}, {cx - 9 + widths[i], y},
            };
            lv_draw_line(ctx, &line, &points[0], &points[1]);
        }
        return;
    }

    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = kind == PLAYER_ICON_PLAY ? C_ACCENT : C_INK;
    if (kind == PLAYER_ICON_PREV || kind == PLAYER_ICON_NEXT) {
        int direction = kind == PLAYER_ICON_NEXT ? 1 : -1;
        lv_point_t triangle[3] = {
            {cx - direction * 7, cy - 9},
            {cx + direction * 8, cy},
            {cx - direction * 7, cy + 9},
        };
        lv_draw_polygon(ctx, &fill, triangle, 3);
        return;
    }

    static const int8_t play_left[4][2] = {{-8, -10}, {9, 0}, {9, 0}, {-8, 10}};
    static const int8_t pause_left[4][2] = {{-7, -10}, {-2, -10}, {-2, 10}, {-7, 10}};
    static const int8_t play_right[4][2] = {{9, 0}, {9, 0}, {9, 0}, {9, 0}};
    static const int8_t pause_right[4][2] = {{3, -10}, {8, -10}, {8, 10}, {3, 10}};
    lv_point_t left[4], right[4];
    for (int i = 0; i < 4; ++i) {
        left[i].x = cx + morph_coord(play_left[i][0], pause_left[i][0]);
        left[i].y = cy + morph_coord(play_left[i][1], pause_left[i][1]);
        right[i].x = cx + morph_coord(play_right[i][0], pause_right[i][0]);
        right[i].y = cy + morph_coord(play_right[i][1], pause_right[i][1]);
    }
    lv_draw_polygon(ctx, &fill, left, 4);
    if (s_play_morph > 0) lv_draw_polygon(ctx, &fill, right, 4);
}

static lv_obj_t *mk_player_icon_btn(lv_obj_t *parent, player_icon_t kind,
                                    lv_event_cb_t callback)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(button, C_TINT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_add_event_cb(button, draw_player_icon, LV_EVENT_DRAW_MAIN_END,
                        (void *)(intptr_t)kind);
    if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    return button;
}

static void set_play_morph(void *object, int32_t value)
{
    s_play_morph = value;
    lv_obj_invalidate((lv_obj_t *)object);
}

static void update_play_icon(bool active)
{
    if (!btn_player_play || s_play_icon_active == active) return;
    s_play_icon_active = active;
    lv_anim_del(btn_player_play, set_play_morph);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, btn_player_play);
    lv_anim_set_values(&animation, s_play_morph, active ? 100 : 0);
    lv_anim_set_time(&animation, 180);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&animation, set_play_morph);
    lv_anim_start(&animation);
}

static lv_obj_t *mk_favorite_btn(lv_obj_t *parent, bool checked,
                                  lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(button, C_TINT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, C_TINT, LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    if (checked) lv_obj_add_state(button, LV_STATE_CHECKED);
    lv_obj_add_event_cb(button, draw_favorite_heart, LV_EVENT_DRAW_MAIN_END, NULL);
    if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    return button;
}

static lv_obj_t *build_audio_visual(lv_obj_t *par, int width, int height)
{
    size_t bytes = LV_CANVAS_BUF_SIZE_TRUE_COLOR(width, height);
    visual_buffer = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!visual_buffer) {
        ESP_LOGE(TAG, "音频视觉画布分配失败：%u bytes", (unsigned)bytes);
        return mk_box(par, width, height);
    }
    visual_canvas = lv_canvas_create(par);
    lv_canvas_set_buffer(visual_canvas, visual_buffer, width, height,
                         LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED);
    lv_canvas_fill_bg(visual_canvas, LV_COLOR_CHROMA_KEY, LV_OPA_COVER);
    return visual_canvas;
}

// ------------------------------------------------------------ 播放控制辅助
static void play_station(station_src_t src, int idx)
{
    const station_t *st = src_get(src, idx);
    if (!st) return;
    s_play_st = *st;
    s_play_src = src;
    s_has_station = true;
    save_last_station();
    player_play(&s_play_st);
}

static void step_station(int dir)
{
    if (!s_has_station) return;
    int n = src_count(s_play_src);
    if (n <= 0) {
        s_play_src = STATION_SRC_PRESET;
        n = src_count(s_play_src);
        if (n <= 0) return;
    }
    int cur = src_index_of(s_play_src, s_play_st.url);
    if (cur < 0) cur = 0;
    int next = ((cur + dir) % n + n) % n;
    play_station(s_play_src, next);
}

// ------------------------------------------------------------------ 刷新
static void refresh_player(void);
static void refresh_list(void);
static void refresh_minibar(void);
static void build_about(void);
static void build_diagnostics_screen(void);
static void build_touch_test(void);
static void reset_touch_test(void);
static void build_ota_screen(void);
static void build_wifi(void);
static void build_settings(void);
static void build_player(void);
static void build_list(void);
static void build_filter(void);
static void build_screen_wake_layer(void);
static void on_ota_open(lv_event_t *e);
static lv_obj_t *setup_screen(void);
static lv_obj_t *setup_line(lv_obj_t *par, const char *txt, const lv_font_t *f, lv_color_t c);

static void refresh_wifi(void)
{
    if (!lbl_wifi) return;
    lv_obj_set_style_text_color(lbl_wifi, s_wifi_ok ? C_ACCENT : C_INK2, 0);
    if (lbl_offline) {
        if (s_wifi_ok) lv_obj_add_flag(lbl_offline, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(lbl_offline, LV_OBJ_FLAG_HIDDEN);
    }
    if (home_visual) {
        lv_obj_set_style_opa(home_visual, s_wifi_ok ? LV_OPA_COVER : LV_OPA_0, 0);
        lv_obj_clear_flag(home_visual, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void refresh_ota_banner(void)
{
    if (!home_update_banner || !lbl_state) return;
    ota_mgr_status_t status = { 0 };
    ota_mgr_status(&status);
    bool available = status.state == OTA_MGR_UPDATE_AVAILABLE;
    if (available) {
        char text[64];
        snprintf(text, sizeof(text), "发现新版本 %s", status.available_version);
        lv_label_set_text(home_update_label, text);
        lv_obj_clear_flag(home_update_banner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_state, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(home_update_banner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_state, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_player(void)
{
    if (!lbl_name) return;

    if (s_has_station) {
        lv_label_set_text(lbl_name, s_play_st.name);
        lv_label_set_text(lbl_meta, s_play_st.meta);
    } else {
        lv_label_set_text(lbl_name, "未选择电台");
        lv_label_set_text(lbl_meta, "");
    }

    // 状态文案全部走 CJK 字库，不混 Montserrat 符号（两个字库合不到一个 label 里）
    player_state_t ps = player_state();
    const char *txt;
    if (!s_wifi_ok) txt = "网络未连接 · 点右上角连接网络";
    else switch (ps) {
        case PLAYER_PLAYING:   txt = "正在播放"; break;
        case PLAYER_BUFFERING: txt = "正在连接…"; break;
        case PLAYER_ERROR:     txt = player_error_msg(); break;
        default:               txt = "已暂停"; break;
    }
    lv_label_set_text(lbl_state, txt);
    lv_obj_set_style_text_color(lbl_state, s_wifi_ok && ps == PLAYER_PLAYING ? C_ACCENT : C_INK2, 0);
    refresh_ota_banner();

    if (s_wifi_ok && ps == PLAYER_ERROR) {
        lv_obj_clear_flag(row_err, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(player_controls, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(row_err, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(player_controls, LV_OBJ_FLAG_HIDDEN);
    }

    bool active = (ps == PLAYER_PLAYING || ps == PLAYER_BUFFERING);
    update_play_icon(active);

    int v = player_volume();
    if (sld_vol && lv_slider_get_value(sld_vol) != v) lv_slider_set_value(sld_vol, v, LV_ANIM_OFF);
    if (btn_player_fav) {
        bool fav = s_has_station && stations_fav_contains(s_play_st.url);
        if (fav) lv_obj_add_state(btn_player_fav, LV_STATE_CHECKED);
        else lv_obj_clear_state(btn_player_fav, LV_STATE_CHECKED);
        lv_obj_invalidate(btn_player_fav);
    }

    refresh_minibar();
}

static void refresh_minibar(void)
{
    if (!mini_name) return;
    if (s_has_station) {
        lv_label_set_text(mini_name, s_play_st.name);
    } else {
        lv_label_set_text(mini_name, "未选择电台");
    }
    player_state_t ps = player_state();
    const char *t = "已停止";
    if (ps == PLAYER_PLAYING) t = "播放中";
    else if (ps == PLAYER_BUFFERING) t = "缓冲中…";
    else if (ps == PLAYER_ERROR) t = "失败";
    lv_label_set_text(mini_state, t);
    lv_label_set_text(mini_icon, ps == PLAYER_PLAYING ? LV_SYMBOL_PLAY : LV_SYMBOL_STOP);
}

// ------------------------------------------------------------ 列表事件回调
static void on_row_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    play_station(s_tab, idx);
    lv_scr_load(scr_player);
    refresh_player();
}

static void on_heart_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const station_t *st = src_get(s_tab, idx);
    if (!st) return;
    if (stations_fav_contains(st->url)) {
        stations_fav_remove(st->url);
    } else {
        if (stations_fav_add(st) == ESP_ERR_NO_MEM) {
            ESP_LOGW(TAG, "收藏已满");
        }
    }
    refresh_list();
}

static void on_pager_click(lv_event_t *e)
{
    (void)e;
    int pages = (src_count(STATION_SRC_CATALOG) + CATALOG_PAGE_SIZE - 1) / CATALOG_PAGE_SIZE;
    if (s_catalog_page + 1 < pages) ++s_catalog_page;
    refresh_list();
}

static void on_prev_page(lv_event_t *e)
{
    (void)e;
    if (s_catalog_page > 0) --s_catalog_page;
    refresh_list();
}

static void on_clear_filter(lv_event_t *e)
{
    (void)e;
    s_filter.cat = s_filter.region = 0;
    s_catalog_page = 0;
    refresh_list();
}

static void on_goto_preset(lv_event_t *e)
{
    (void)e;
    s_tab = STATION_SRC_PRESET;
    save_tab();
    refresh_list();
}

// ------------------------------------------------------------- 列表项构建
static void add_row(lv_obj_t *par, const station_t *st, int idx)
{
    bool playing = s_has_station && strcmp(st->url, s_play_st.url) == 0;
    bool fav = stations_fav_contains(st->url);

    lv_obj_t *row = mk_box(par, LV_PCT(100), ROW_H);
    lv_obj_set_style_bg_color(row, playing ? C_TINT : C_SURFACE, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_border_width(row, playing ? 1 : 0, 0);
    lv_obj_set_style_border_color(row, C_ACCENT, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, on_row_click, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    // 正在播放：靠左边一道竖线标识（省一个子对象）
    if (playing) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(row, C_ACCENT, 0);
        lv_obj_set_style_border_width(row, 3, 0);
    }

    lv_obj_t *name = mk_label(row, st->name, &font_cjk_18, C_INK);
    // LONG_DOT 只有在宽高都受约束时才会截断；仅设宽度会先增高并换行。
    lv_obj_set_size(name, 194, 22);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(name, 8, 2);

    lv_obj_t *meta = mk_label(row, st->meta, &font_cjk_14, C_INK2);
    lv_obj_set_size(meta, 194, 18);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(meta, 8, 24);

    lv_obj_t *heart = mk_favorite_btn(row, fav, on_heart_click, (void *)(intptr_t)idx);
    lv_obj_set_size(heart, 44, ROW_H - 2);
    lv_obj_align(heart, LV_ALIGN_RIGHT_MID, -2, 1);
}

// 空态 / 错误态：标题 + 说明 + 最多两个动作按钮
static void add_statebox(lv_obj_t *par, const char *t1, const char *t2,
                         const char *a1, lv_event_cb_t cb1,
                         const char *a2, lv_event_cb_t cb2)
{
    lv_obj_t *box = mk_box(par, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(box, 18, 0);
    lv_obj_set_style_pad_left(box, 10, 0);
    lv_obj_set_style_pad_right(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 6, 0);

    lv_obj_t *l1 = mk_label(box, t1, &font_cjk_18, C_INK);
    lv_obj_set_style_text_align(l1, LV_TEXT_ALIGN_CENTER, 0);

    if (t2 && t2[0]) {
        lv_obj_t *l2 = mk_label(box, t2, &font_cjk_14, C_INK2);
        lv_obj_set_width(l2, 220);
        lv_label_set_long_mode(l2, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(l2, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (a1) {
        lv_obj_t *acts = mk_box(box, LV_SIZE_CONTENT, 36);
        lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(acts, 8, 0);
        lv_obj_t *b1 = mk_btn(acts, a1, &font_cjk_14, true, cb1, NULL);
        lv_obj_set_size(b1, 104, 36);
        if (a2) {
            lv_obj_t *b2 = mk_btn(acts, a2, &font_cjk_14, false, cb2, NULL);
            lv_obj_set_size(b2, 104, 36);
        }
    }
}

static void refresh_list(void)
{
    if (!list_content) return;

    lv_obj_clean(list_content);

    const char *titles[3] = { "我的收藏", "编辑精选", "国内电台" };
    lv_label_set_text(lbl_title, titles[s_tab]);

    // tab 选中态
    for (int i = 0; i < 3; i++) {
        bool on = (i == (int)s_tab);
        lv_obj_set_style_bg_color(tab_btn[i], on ? C_ACCENT : C_PAPER, 0);
        lv_obj_set_style_text_color(tab_lbl[i], on ? C_ON_ACCENT : C_INK2, 0);
    }

    // 完整台库可按内容类型与省份筛选；精选和收藏保持一步直达。
    if (s_tab == STATION_SRC_CATALOG) {
        lv_obj_clear_flag(btn_filter, LV_OBJ_FLAG_HIDDEN);
        int n = filter_active_count(&s_filter);
        char buf[24];
        if (n > 0) snprintf(buf, sizeof(buf), "筛选·%d", n);
        else snprintf(buf, sizeof(buf), "筛选");
        lv_label_set_text(lbl_filter, buf);
    } else {
        lv_obj_add_flag(btn_filter, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_tab == STATION_SRC_FAV && stations_fav_count() == 0) {
        add_statebox(list_content, "还没有收藏电台", "在精选或台库里点心形按钮收藏",
                     "去看精选", on_goto_preset, NULL, NULL);
        return;
    }

    int n = src_count(s_tab);
    if (s_tab == STATION_SRC_CATALOG && n == 0) {
        add_statebox(list_content, "没有符合条件的电台", "换个地区或分类试试",
                     "清除筛选", on_clear_filter, NULL, NULL);
        return;
    }

    int first = s_tab == STATION_SRC_CATALOG ? s_catalog_page * CATALOG_PAGE_SIZE : 0;
    int last = s_tab == STATION_SRC_CATALOG ? first + CATALOG_PAGE_SIZE : n;
    if (last > n) last = n;
    for (int i = first; i < last; i++) {
        const station_t *st = src_get(s_tab, i);
        if (st) add_row(list_content, st, i);
    }

    if (s_tab == STATION_SRC_CATALOG) {
        int pages = (n + CATALOG_PAGE_SIZE - 1) / CATALOG_PAGE_SIZE;
        lv_obj_t *pager = mk_box(list_content, LV_PCT(100), 40);
        lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pager, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(pager, 8, 0);
        lv_obj_t *prev = mk_btn(pager, LV_SYMBOL_LEFT, &lv_font_montserrat_14, false,
                                on_prev_page, NULL);
        lv_obj_set_size(prev, 44, 34);
        if (s_catalog_page == 0) lv_obj_add_state(prev, LV_STATE_DISABLED);
        char buf[48];
        snprintf(buf, sizeof(buf), "%d / %d · 共 %d 台", s_catalog_page + 1, pages, n);
        lv_obj_t *page = mk_label(pager, buf, &font_cjk_14, C_INK2);
        lv_obj_set_width(page, 120);
        lv_obj_set_style_text_align(page, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *next = mk_btn(pager, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, false,
                                on_pager_click, NULL);
        lv_obj_set_size(next, 44, 34);
        if (s_catalog_page + 1 >= pages) lv_obj_add_state(next, LV_STATE_DISABLED);
    }
}

// --------------------------------------------------------------- 播放页事件
static void on_toggle_play(lv_event_t *e)
{
    (void)e;
    player_state_t ps = player_state();
    if (ps == PLAYER_PLAYING || ps == PLAYER_BUFFERING) {
        player_stop();
    } else if (s_has_station) {
        player_play(&s_play_st);
    }
    refresh_player();
}

static void on_player_fav(lv_event_t *e)
{
    (void)e;
    if (!s_has_station) return;
    if (stations_fav_contains(s_play_st.url)) stations_fav_remove(s_play_st.url);
    else stations_fav_add(&s_play_st);
    refresh_player();
    refresh_list();
}

static void on_prev(lv_event_t *e) { (void)e; step_station(-1); refresh_player(); }
static void on_next(lv_event_t *e) { (void)e; step_station(1); refresh_player(); }

static void on_retry_play(lv_event_t *e)
{
    (void)e;
    if (s_has_station) player_play(&s_play_st);
    refresh_player();
}

static void on_goto_list(lv_event_t *e)
{
    (void)e;
    refresh_list();
    lv_scr_load(scr_list);
}

static void on_goto_player(lv_event_t *e)
{
    (void)e;
    refresh_player();
    lv_scr_load(scr_player);
}

static void on_vol_change(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    player_set_volume(v);
}

static void on_vol_release(lv_event_t *e)
{
    (void)e;
    player_save_volume();
}

static void on_tab_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_tab = (station_src_t)idx;
    save_tab();
    s_catalog_page = 0;
    refresh_list();
}

// --------------------------------------------------------------- 筛选页事件
static void refresh_chips(void)
{
    for (int i = 0; i < stations_cat_count(); i++) {
        bool on = (i == s_draft.cat);
        lv_obj_set_style_bg_color(chip_cat[i], on ? C_ACCENT : C_SURFACE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(chip_cat[i], 0), on ? C_ON_ACCENT : C_INK, 0);
    }
    if (region_dropdown) lv_dropdown_set_selected(region_dropdown, s_draft.region);
}

static void on_chip_cat(lv_event_t *e)  { s_draft.cat  = (int)(intptr_t)lv_event_get_user_data(e); refresh_chips(); }
static void on_region_change(lv_event_t *e)
{
    s_draft.region = lv_dropdown_get_selected(lv_event_get_target(e));
}

static void on_filter_open(lv_event_t *e)
{
    (void)e;
    s_draft = s_filter;
    refresh_chips();
    lv_scr_load(scr_filter);
}

static void on_filter_cancel(lv_event_t *e)
{
    (void)e;
    lv_scr_load(scr_list);
}

static void on_filter_reset(lv_event_t *e)
{
    (void)e;
    s_draft.cat = s_draft.region = 0;
    refresh_chips();
}

static void on_filter_apply(lv_event_t *e)
{
    (void)e;
    s_filter = s_draft;
    s_catalog_page = 0;
    refresh_list();
    lv_scr_load(scr_list);
}

// ------------------------------------------------------------- 界面构建
// ------------------------------------------------------------------ 设置页
static void update_sleep_label(void);
static void update_sleep_countdown(void);

static void set_display_awake_locked(bool awake)
{
    if (awake == s_screen_awake) return;
    s_screen_awake = awake;
    if (awake) {
        board_display_power(true);
        if (screen_wake_layer) lv_obj_add_flag(screen_wake_layer, LV_OBJ_FLAG_HIDDEN);
        if (s_theme_rebuild_pending) request_theme_rebuild();
    } else {
        if (drawer_layer) lv_obj_add_flag(drawer_layer, LV_OBJ_FLAG_HIDDEN);
        if (donation_layer) lv_obj_add_flag(donation_layer, LV_OBJ_FLAG_HIDDEN);
        if (screen_wake_layer) {
            lv_obj_clear_flag(screen_wake_layer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(screen_wake_layer);
        }
        board_display_power(false);
    }
}

static void on_screen_wake(lv_event_t *e)
{
    (void)e;
    // 透明遮罩会吞掉这一次完整触摸，因此唤醒不会误触底层按钮。
    set_display_awake_locked(true);
}

static void on_screen_off(lv_event_t *e)
{
    (void)e;
    set_display_awake_locked(false);
}

static void sleep_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    s_sleep_expired = true;
}

static void update_sleep_label(void)
{
    if (!drawer_sleep_label) return;
    char text[16];
    if (s_sleep_minutes == 0) snprintf(text, sizeof(text), "睡眠关闭");
    else snprintf(text, sizeof(text), "睡眠%d分", s_sleep_minutes);
    lv_label_set_text(drawer_sleep_label, text);
}

static void update_sleep_countdown(void)
{
    if (!top_sleep_group || !lbl_sleep_countdown) return;
    int64_t deadline = s_sleep_deadline_us;
    if (deadline <= 0) {
        lv_obj_add_flag(top_sleep_group, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    int64_t left_us = deadline - esp_timer_get_time();
    unsigned seconds = left_us > 0 ? (unsigned)((left_us + 999999) / 1000000) : 0;
    if (seconds > 90U * 60U) seconds = 90U * 60U;
    char text[12];
    snprintf(text, sizeof(text), "%u:%02u", seconds / 60U, seconds % 60U);
    lv_label_set_text(lbl_sleep_countdown, text);
    lv_obj_clear_flag(top_sleep_group, LV_OBJ_FLAG_HIDDEN);
}

static esp_err_t set_sleep_timer_seconds_locked(int seconds)
{
    if (seconds < 0 || seconds > 90 * 60) return ESP_ERR_INVALID_ARG;
    if (!s_sleep_timer) {
        s_sleep_timer = xTimerCreate("sleep", pdMS_TO_TICKS(60000), pdFALSE, NULL, sleep_timer_cb);
        if (!s_sleep_timer) return ESP_ERR_NO_MEM;
    }
    if (xTimerStop(s_sleep_timer, 0) != pdPASS) return ESP_FAIL;
    s_sleep_expired = false;
    if (seconds > 0) {
        // xTimerChangePeriod 会同时启动休眠中的一次性定时器，不再重复 xTimerStart。
        if (xTimerChangePeriod(s_sleep_timer, pdMS_TO_TICKS(seconds * 1000), 0) != pdPASS)
            return ESP_FAIL;
    }
    s_sleep_minutes = seconds > 0 ? (seconds + 59) / 60 : 0;
    s_sleep_deadline_us = seconds > 0 ? esp_timer_get_time() + (int64_t)seconds * 1000000 : 0;
    update_sleep_label();
    update_sleep_countdown();
    return ESP_OK;
}

static void on_sleep_cycle(lv_event_t *e)
{
    (void)e;
    static const int choices[] = { 0, 15, 30, 60, 90 };
    int pos = 0;
    for (int i = 0; i < 5; i++) if (choices[i] == s_sleep_minutes) pos = i;
    s_sleep_minutes = choices[(pos + 1) % 5];
    set_sleep_timer_seconds_locked(s_sleep_minutes * 60);
}

static void on_auto_change(lv_event_t *e)
{
    s_autoplay = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    save_i32(NVS_KEY_AUTO, s_autoplay ? 1 : 0);
}

static void on_brightness_change(lv_event_t *e)
{
    s_brightness = lv_slider_get_value(lv_event_get_target(e));
    board_backlight_level(s_brightness);
}

static void on_brightness_release(lv_event_t *e)
{
    (void)e;
    save_i32(NVS_KEY_BRIGHT, s_brightness);
}

static void refresh_wifi_list(void);
static void on_wifi_rescan(lv_event_t *e);

static void wifi_scan_task(void *arg)
{
    (void)arg;
    size_t n = 0;
    esp_err_t err = wifi_mgr_scan(s_wifi_items, WIFI_MGR_SCAN_MAX, &n);
    s_wifi_count = err == ESP_OK ? n : 0;
    s_wifi_scanning = false;
    if (lvgl_port_lock(2000)) {
        if (s_wifi_visible) refresh_wifi_list();
        lvgl_port_unlock();
    }
    vTaskDelete(NULL);
}

static void start_wifi_scan(void)
{
    if (s_wifi_scanning) return;
    s_wifi_visible = true;
    s_wifi_scanning = true;
    s_wifi_count = 0;
    lv_obj_clean(wifi_content);
    add_statebox(wifi_content, "正在扫描附近网络…", "只显示 2.4GHz Wi-Fi", NULL, NULL, NULL, NULL);
    if (xTaskCreate(wifi_scan_task, "ui_wifi_scan", 4096, NULL, 4, NULL) != pdPASS) {
        s_wifi_scanning = false;
        lv_obj_clean(wifi_content);
        add_statebox(wifi_content, "扫描暂时不可用", "设备内存不足，请稍后重试",
                     "重试", on_wifi_rescan, NULL, NULL);
    }
}

static void on_wifi_rescan(lv_event_t *e)
{
    (void)e;
    start_wifi_scan();
}

static void on_wifi_back(lv_event_t *e)
{
    (void)e;
    s_wifi_visible = false;
    s_wifi_count = 0;
    lv_obj_clean(wifi_content);  // 扫描行仅在此页需要，离开即归还紧张的内部 RAM
    lv_scr_load(scr_settings);
}

static void on_wifi_password_back(lv_event_t *e)
{
    (void)e;
    s_wifi_visible = true;
    refresh_wifi_list();
    lv_scr_load(scr_wifi);
}

static void on_wifi_submit(lv_event_t *e)
{
    (void)e;
    const char *ssid = lv_textarea_get_text(wifi_ssid_input);
    const char *pass = lv_textarea_get_text(wifi_password);
    if (!ssid || !ssid[0]) {
        lv_textarea_set_placeholder_text(wifi_ssid_input, "请输入 Wi-Fi 名称");
        lv_keyboard_set_textarea(wifi_keyboard, wifi_ssid_input);
        lv_obj_add_state(wifi_ssid_input, LV_STATE_FOCUSED);
        return;
    }
    ui_show_setup_connecting(ssid);
    wifi_mgr_connect(ssid, pass);
}

static void on_wifi_field_focus(lv_event_t *e)
{
    lv_keyboard_set_textarea(wifi_keyboard, lv_event_get_target(e));
}

static void log_wifi_password_layout(void)
{
    lv_obj_update_layout(scr_wifi_pass);
    lv_area_t ssid_area;
    lv_area_t password_area;
    lv_area_t keyboard_area;
    lv_obj_get_coords(wifi_ssid_input, &ssid_area);
    lv_obj_get_coords(wifi_password, &password_area);
    lv_obj_get_coords(wifi_keyboard, &keyboard_area);
    ESP_LOGI(TAG,
             "Wi-Fi 输入布局：ssid=%d..%d password=%d..%d keyboard=%d..%d gap=%d",
             ssid_area.y1, ssid_area.y2, password_area.y1, password_area.y2,
             keyboard_area.y1, keyboard_area.y2,
             keyboard_area.y1 - password_area.y2 - 1);
}

static void on_wifi_row(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_wifi_visible = false;
    if (idx == -1) {
        s_wifi_selected = -1;
        lv_textarea_set_text(wifi_ssid_input, "");
        lv_textarea_set_text(wifi_password, "");
        lv_keyboard_set_textarea(wifi_keyboard, wifi_ssid_input);
        log_wifi_password_layout();
        lv_scr_load(scr_wifi_pass);
        return;
    }
    if (idx < 0 || idx >= (int)s_wifi_count) return;
    s_wifi_selected = idx;
    if (!s_wifi_items[idx].secured) {
        ui_show_setup_connecting(s_wifi_items[idx].ssid);
        wifi_mgr_connect(s_wifi_items[idx].ssid, "");
        return;
    }
    lv_textarea_set_text(wifi_ssid_input, s_wifi_items[idx].ssid);
    lv_textarea_set_text(wifi_password, "");
    lv_keyboard_set_textarea(wifi_keyboard, wifi_password);
    log_wifi_password_layout();
    lv_scr_load(scr_wifi_pass);
}

static void refresh_wifi_list(void)
{
    if (!wifi_content) return;
    lv_obj_clean(wifi_content);
    if (s_wifi_count == 0) {
        add_statebox(wifi_content, "没有找到可用网络", "请靠近路由器后重试",
                     "重新扫描", on_wifi_rescan, NULL, NULL);
        lv_obj_t *manual = mk_btn(wifi_content, "隐藏网络 / 手动输入", &font_cjk_14, false,
                                  on_wifi_row, (void *)(intptr_t)-1);
        lv_obj_set_size(manual, LV_PCT(100), 44);
        return;
    }
    char current[33];
    wifi_mgr_current_ssid(current, sizeof(current));
    for (int i = 0; i < (int)s_wifi_count; i++) {
        lv_obj_t *row = mk_box(wifi_content, LV_PCT(100), 48);
        lv_obj_set_style_bg_color(row, C_SURFACE, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_left(row, 12, 0);
        lv_obj_set_style_pad_right(row, 12, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_wifi_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *name = mk_label(row, s_wifi_items[i].ssid, &font_cjk_18, C_INK);
        lv_obj_set_width(name, 220);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
        const char *state = strcmp(current, s_wifi_items[i].ssid) == 0 && s_wifi_ok ? "已连接" :
                            (s_wifi_items[i].rssi > -58 ? "强" : (s_wifi_items[i].rssi > -72 ? "中" : "弱"));
        lv_obj_t *sig = mk_label(row, state, &font_cjk_14,
                                 strcmp(state, "已连接") == 0 ? C_ACCENT : C_INK2);
        lv_obj_align(sig, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    lv_obj_t *manual = mk_btn(wifi_content, "隐藏网络 / 手动输入", &font_cjk_14, false,
                              on_wifi_row, (void *)(intptr_t)-1);
    lv_obj_set_size(manual, LV_PCT(100), 44);
}

static void on_wifi_open(lv_event_t *e)
{
    (void)e;
    lv_scr_load(scr_wifi);
    start_wifi_scan();
}

static void build_wifi(void)
{
    scr_wifi = lv_obj_create(NULL);
    plain(scr_wifi);
    lv_obj_set_flex_flow(scr_wifi, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *hdr = mk_box(scr_wifi, LV_PCT(100), 40);
    lv_obj_t *back = mk_btn(hdr, LV_SYMBOL_LEFT, &lv_font_montserrat_14, false, on_wifi_back, NULL);
    lv_obj_set_size(back, 48, 40); lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_t *title = mk_label(hdr, "选择 Wi-Fi", &font_cjk_18, C_INK); lv_obj_center(title);
    lv_obj_t *scan = mk_btn(hdr, "刷新", &font_cjk_14, false, on_wifi_rescan, NULL);
    lv_obj_set_size(scan, 56, 40); lv_obj_set_style_border_width(scan, 0, 0);
    lv_obj_align(scan, LV_ALIGN_RIGHT_MID, 0, 0);
    wifi_content = mk_box(scr_wifi, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(wifi_content, 1);
    lv_obj_set_flex_flow(wifi_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(wifi_content, 8, 0);
    lv_obj_set_style_pad_row(wifi_content, 8, 0);
    lv_obj_add_flag(wifi_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(wifi_content, LV_DIR_VER);

    scr_wifi_pass = lv_obj_create(NULL);
    plain(scr_wifi_pass);
    lv_obj_set_flex_flow(scr_wifi_pass, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr_wifi_pass, 0, 0);

    lv_obj_t *pass_header = mk_box(scr_wifi_pass, LV_PCT(100), 36);
    lv_obj_t *pback = mk_btn(pass_header, LV_SYMBOL_LEFT, &lv_font_montserrat_14, false,
                             on_wifi_password_back, NULL);
    lv_obj_set_size(pback, 48, 36); lv_obj_set_style_border_width(pback, 0, 0);
    lv_obj_set_style_bg_opa(pback, LV_OPA_TRANSP, 0);
    lv_obj_t *ptitle = mk_label(pass_header, "连接 Wi-Fi", &font_cjk_18, C_INK);
    lv_obj_set_width(ptitle, 240); lv_obj_set_style_text_align(ptitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ptitle, 40, 6);

    // 输入区与键盘是连续操作面：由 flex 计算位置，不留任何不可点的中间空白。
    lv_obj_t *pass_form = mk_box(scr_wifi_pass, LV_PCT(100), 64);
    wifi_ssid_input = lv_textarea_create(pass_form);
    lv_obj_set_size(wifi_ssid_input, 304, 30); lv_obj_set_pos(wifi_ssid_input, 8, 1);
    lv_textarea_set_one_line(wifi_ssid_input, true);
    lv_textarea_set_max_length(wifi_ssid_input, 32);
    lv_textarea_set_placeholder_text(wifi_ssid_input, "Wi-Fi 名称");
    lv_obj_set_style_text_font(wifi_ssid_input, &font_cjk_14, 0);
    lv_obj_set_style_pad_top(wifi_ssid_input, 5, 0);
    lv_obj_set_style_pad_bottom(wifi_ssid_input, 5, 0);
    lv_obj_set_style_bg_color(wifi_ssid_input, C_SURFACE, 0);
    lv_obj_set_style_text_color(wifi_ssid_input, C_INK, 0);
    lv_obj_set_style_border_color(wifi_ssid_input, C_ACCENT, LV_STATE_FOCUSED);
    wifi_password = lv_textarea_create(pass_form);
    lv_obj_set_size(wifi_password, 304, 30); lv_obj_set_pos(wifi_password, 8, 33);
    lv_textarea_set_one_line(wifi_password, true);
    lv_textarea_set_max_length(wifi_password, 63);
    lv_textarea_set_password_mode(wifi_password, true);
    lv_textarea_set_placeholder_text(wifi_password, "输入 Wi-Fi 密码");
    lv_obj_set_style_text_font(wifi_password, &font_cjk_14, 0);
    lv_obj_set_style_pad_top(wifi_password, 5, 0);
    lv_obj_set_style_pad_bottom(wifi_password, 5, 0);
    lv_obj_set_style_bg_color(wifi_password, C_SURFACE, 0);
    lv_obj_set_style_text_color(wifi_password, C_INK, 0);
    lv_obj_set_style_border_color(wifi_password, C_ACCENT, LV_STATE_FOCUSED);
    wifi_keyboard = lv_keyboard_create(scr_wifi_pass);
    // 240 - 36 - 64 = 140 px：键盘紧接输入框并占满所有剩余空间。
    lv_obj_set_size(wifi_keyboard, LV_PCT(100), 0);
    lv_obj_set_flex_grow(wifi_keyboard, 1);
    lv_keyboard_set_textarea(wifi_keyboard, wifi_password);
    lv_obj_set_style_bg_color(wifi_keyboard, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wifi_keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(wifi_keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(wifi_keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wifi_keyboard, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_row(wifi_keyboard, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_column(wifi_keyboard, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(wifi_keyboard, C_WHITE, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(wifi_keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(wifi_keyboard, C_LINE, LV_PART_ITEMS);
    lv_obj_set_style_border_width(wifi_keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(wifi_keyboard, C_ACCENT,
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(wifi_keyboard, C_ON_ACCENT,
                                LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(wifi_keyboard, C_INK, LV_PART_ITEMS);
    lv_obj_set_style_text_font(wifi_keyboard, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_radius(wifi_keyboard, 6, LV_PART_ITEMS);
    lv_obj_add_event_cb(wifi_ssid_input, on_wifi_field_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(wifi_password, on_wifi_field_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(wifi_keyboard, on_wifi_submit, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(wifi_keyboard, on_wifi_password_back, LV_EVENT_CANCEL, NULL);
}

static void on_close_settings(lv_event_t *e)
{
    (void)e;
    refresh_player();
    lv_scr_load(scr_player);
}

static bool is_info_screen(lv_obj_t *screen)
{
    return screen && (screen == scr_about || screen == scr_ota || screen == scr_diagnostics);
}

static void release_info_screen(lv_obj_t *screen)
{
    if (!is_info_screen(screen)) return;
    if (screen == scr_about) scr_about = NULL;
    if (screen == scr_ota) {
        scr_ota = NULL;
        ota_body = NULL;
    }
    if (screen == scr_diagnostics) {
        scr_diagnostics = NULL;
        diagnostics_body = NULL;
    }
    lv_obj_del_async(screen);
}

static void on_about_open(lv_event_t *e)
{
    (void)e;
    if (!scr_about) build_about();
    lv_scr_load(scr_about);
}
static void refresh_diagnostics_screen(void);
static void on_diagnostics_open(lv_event_t *e)
{
    (void)e;
    if (!scr_diagnostics) build_diagnostics_screen();
    refresh_diagnostics_screen();
    lv_scr_load(scr_diagnostics);
}

static void refresh_time_format_buttons(void)
{
    for (int i = 0; i < 2; i++) {
        if (!settings_time_btn[i]) continue;
        bool selected = (i == 0) == s_time_24h;
        lv_obj_set_style_bg_color(settings_time_btn[i], selected ? C_ACCENT : C_SURFACE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(settings_time_btn[i], 0),
                                    selected ? C_ON_ACCENT : C_INK2, 0);
    }
}

static void on_time_format(lv_event_t *e)
{
    int fmt = (int)(intptr_t)lv_event_get_user_data(e);
    s_time_24h = fmt == 0;
    save_i32(NVS_KEY_TIMEFMT, s_time_24h ? 24 : 12);
    refresh_time_format_buttons();
}

static const char THEME_HOUR_OPTIONS[] =
    "00:00\n01:00\n02:00\n03:00\n04:00\n05:00\n06:00\n07:00\n"
    "08:00\n09:00\n10:00\n11:00\n12:00\n13:00\n14:00\n15:00\n"
    "16:00\n17:00\n18:00\n19:00\n20:00\n21:00\n22:00\n23:00";

static void refresh_theme_mode_buttons(void)
{
    for (int i = 0; i < 3; ++i) {
        if (!settings_theme_btn[i]) continue;
        bool selected = i == (int)s_theme_mode;
        lv_obj_set_style_bg_color(settings_theme_btn[i], selected ? C_ACCENT : C_SURFACE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(settings_theme_btn[i], 0),
                                    selected ? C_ON_ACCENT : C_INK2, 0);
    }
    if (settings_schedule_row) {
        if (s_theme_mode == THEME_MODE_AUTO)
            lv_obj_clear_flag(settings_schedule_row, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(settings_schedule_row, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_theme_mode(lv_event_t *e)
{
    int mode = (int)(intptr_t)lv_event_get_user_data(e);
    if (mode < THEME_MODE_LIGHT || mode > THEME_MODE_AUTO) return;
    if (mode == s_theme_mode && mode != THEME_MODE_AUTO) return;
    set_theme_mode_locked((theme_mode_t)mode);
}

static void on_theme_schedule(lv_event_t *e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    int hour = (int)lv_dropdown_get_selected(dropdown);
    if (dropdown == settings_dark_start) {
        set_dark_schedule_locked(hour, s_dark_end_hour);
    } else {
        set_dark_schedule_locked(s_dark_start_hour, hour);
    }
}

static void draw_schedule_connector(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    lv_coord_t cy = (area.y1 + area.y2) / 2;
    lv_point_t points[2] = {{area.x1, cy}, {area.x2, cy}};
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = C_INK2;
    line.width = 2;
    line.round_start = true;
    line.round_end = true;
    lv_draw_line(ctx, &line, &points[0], &points[1]);
}

static void style_hour_dropdown(lv_obj_t *dropdown)
{
    lv_dropdown_set_options_static(dropdown, THEME_HOUR_OPTIONS);
    lv_obj_set_style_bg_color(dropdown, C_SURFACE, 0);
    lv_obj_set_style_text_color(dropdown, C_INK, 0);
    lv_obj_set_style_text_font(dropdown, &lv_font_montserrat_14, 0);
    lv_obj_set_style_border_width(dropdown, 0, 0);
    lv_obj_set_style_radius(dropdown, 10, 0);
    lv_obj_set_style_pad_left(dropdown, 10, 0);
    lv_dropdown_open(dropdown);
    lv_obj_t *list = lv_dropdown_get_list(dropdown);
    if (list) {
        lv_obj_set_style_bg_color(list, C_SURFACE, 0);
        lv_obj_set_style_text_color(list, C_INK, 0);
        lv_obj_set_style_text_font(list, &lv_font_montserrat_14, 0);
        lv_obj_set_style_border_color(list, C_LINE, 0);
        lv_obj_set_style_bg_color(list, C_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(list, C_ON_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
    }
    lv_dropdown_close(dropdown);
    lv_obj_add_event_cb(dropdown, on_theme_schedule, LV_EVENT_VALUE_CHANGED, NULL);
}

static void refresh_theme_picker(void)
{
    if (!theme_picker_preview) return;
    lv_color_t selected = theme_accent_color_for(s_picker_pending_accent,
                                                  s_picker_pending_hue, s_theme_dark);
    lv_obj_set_style_bg_color(theme_picker_preview, selected, 0);
    if (theme_picker_value) {
        char value[32];
        if (s_picker_pending_accent == 4) {
            snprintf(value, sizeof(value), "自定义 %d°", s_picker_pending_hue);
        } else {
            snprintf(value, sizeof(value), "%s", theme_accent_name(s_picker_pending_accent));
        }
        lv_label_set_text(theme_picker_value, value);
    }
    for (int i = 0; i < 4; ++i) {
        if (!theme_picker_preset_btn[i]) continue;
        bool selected_preset = s_picker_pending_accent == i;
        lv_obj_set_style_bg_opa(theme_picker_preset_btn[i],
                                selected_preset ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(theme_picker_preset_btn[i], C_SURFACE, 0);
    }
}

static void on_theme_picker_wheel(lv_event_t *e)
{
    if (s_picker_syncing) return;
    lv_color_hsv_t hsv = lv_colorwheel_get_hsv(lv_event_get_target(e));
    s_picker_pending_accent = 4;
    s_picker_pending_hue = hsv.h;
    refresh_theme_picker();
}

static void on_theme_picker_preset(lv_event_t *e)
{
    int accent = (int)(intptr_t)lv_event_get_user_data(e);
    if (accent < 0 || accent >= 4) return;
    s_picker_pending_accent = accent;
    lv_color_hsv_t hsv = lv_color_to_hsv(lv_color_hex(THEME_ACCENT_LIGHT[accent]));
    s_picker_pending_hue = hsv.h;
    if (theme_picker_wheel) {
        s_picker_syncing = true;
        lv_colorwheel_set_hsv(theme_picker_wheel, (lv_color_hsv_t){hsv.h, 72, 82});
        s_picker_syncing = false;
    }
    refresh_theme_picker();
}

static void on_theme_picker_close(lv_event_t *e)
{
    (void)e;
    if (!theme_picker_layer) return;
    lv_obj_t *layer = theme_picker_layer;
    theme_picker_layer = theme_picker_wheel = theme_picker_preview = theme_picker_value = NULL;
    memset(theme_picker_preset_btn, 0, sizeof(theme_picker_preset_btn));
    lv_obj_del_async(layer);
}

static void on_theme_picker_backdrop(lv_event_t *e)
{
    if (lv_event_get_target(e) == theme_picker_layer) on_theme_picker_close(e);
}

static void on_theme_picker_apply(lv_event_t *e)
{
    (void)e;
    bool changed = s_theme_accent != s_picker_pending_accent ||
                   (s_picker_pending_accent == 4 && s_theme_custom_hue != s_picker_pending_hue);
    s_theme_accent = s_picker_pending_accent;
    s_theme_custom_hue = s_picker_pending_hue;
    save_i32(NVS_KEY_THEME_ACCENT, s_theme_accent);
    save_i32(NVS_KEY_CUSTOM_HUE, s_theme_custom_hue);
    if (changed) {
        if (theme_picker_layer) lv_obj_add_flag(theme_picker_layer, LV_OBJ_FLAG_HIDDEN);
        request_theme_rebuild();
    } else {
        on_theme_picker_close(NULL);
    }
}

static void build_theme_picker(void)
{
    theme_picker_layer = mk_box(scr_settings, 320, 240);
    lv_obj_add_flag(theme_picker_layer, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(theme_picker_layer, 0, 0);
    lv_obj_set_style_bg_color(theme_picker_layer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(theme_picker_layer, LV_OPA_50, 0);
    lv_obj_add_event_cb(theme_picker_layer, on_theme_picker_backdrop, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = mk_box(theme_picker_layer, 296, 224);
    lv_obj_center(card);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(card, C_PAPER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, C_LINE, 0);

    lv_obj_t *title = mk_label(card, "选择主题色", &font_cjk_18, C_INK);
    lv_obj_set_pos(title, 16, 12);
    lv_obj_t *close = mk_btn(card, LV_SYMBOL_CLOSE, &lv_font_montserrat_14,
                             false, on_theme_picker_close, NULL);
    lv_obj_set_size(close, 44, 40);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);

    theme_picker_wheel = lv_colorwheel_create(card, true);
    lv_obj_set_size(theme_picker_wheel, 116, 116);
    lv_obj_set_pos(theme_picker_wheel, 24, 44);
    lv_colorwheel_set_mode(theme_picker_wheel, LV_COLORWHEEL_MODE_HUE);
    lv_colorwheel_set_mode_fixed(theme_picker_wheel, true);
    lv_obj_set_style_arc_width(theme_picker_wheel, 14, LV_PART_MAIN);
    lv_obj_add_event_cb(theme_picker_wheel, on_theme_picker_wheel,
                        LV_EVENT_VALUE_CHANGED, NULL);

    theme_picker_preview = mk_box(card, 52, 52);
    lv_obj_set_pos(theme_picker_preview, 190, 50);
    lv_obj_set_style_radius(theme_picker_preview, LV_RADIUS_CIRCLE, 0);

    theme_picker_value = mk_label(card, "", &font_cjk_14, C_INK2);
    lv_obj_set_width(theme_picker_value, 124);
    lv_obj_set_style_text_align(theme_picker_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(theme_picker_value, 154, 106);

    lv_obj_t *apply = mk_btn(card, "应用", &font_cjk_14, true,
                             on_theme_picker_apply, NULL);
    lv_obj_set_size(apply, 112, 40);
    lv_obj_set_pos(apply, 160, 124);

    for (int i = 0; i < 4; ++i) {
        lv_obj_t *button = lv_btn_create(card);
        theme_picker_preset_btn[i] = button;
        lv_obj_set_size(button, 52, 44);
        lv_obj_set_pos(button, 16 + i * 68, 172);
        lv_obj_set_style_radius(button, 10, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_t *dot = mk_box(button, 24, 24);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(THEME_ACCENT_LIGHT[i]), 0);
        lv_obj_center(dot);
        lv_obj_add_event_cb(button, on_theme_picker_preset, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
    lv_obj_add_flag(theme_picker_layer, LV_OBJ_FLAG_HIDDEN);
}

static void on_theme_picker_open(lv_event_t *e)
{
    (void)e;
    if (!theme_picker_layer) build_theme_picker();
    s_picker_pending_accent = s_theme_accent;
    s_picker_pending_hue = s_theme_custom_hue;
    int wheel_hue = s_picker_pending_hue;
    if (s_picker_pending_accent >= 0 && s_picker_pending_accent < 4) {
        wheel_hue = lv_color_to_hsv(lv_color_hex(THEME_ACCENT_LIGHT[s_picker_pending_accent])).h;
    }
    s_picker_syncing = true;
    lv_colorwheel_set_hsv(theme_picker_wheel, (lv_color_hsv_t){wheel_hue, 72, 82});
    s_picker_syncing = false;
    refresh_theme_picker();
    lv_obj_move_foreground(theme_picker_layer);
    lv_obj_clear_flag(theme_picker_layer, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *setting_nav_row(lv_obj_t *body, const char *name, const char *value,
                                 lv_event_cb_t cb)
{
    lv_obj_t *row = lv_btn_create(body);
    lv_obj_set_size(row, LV_PCT(100), ROW_H);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, C_LINE, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_bg_color(row, C_PAPER, 0);
    lv_obj_set_style_bg_color(row, C_TINT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_t *name_label = mk_label(row, name, &font_cjk_14, C_INK);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 8, 0);
    if (value) {
        lv_obj_t *value_label = mk_label(row, value, &font_cjk_14, C_INK2);
        lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, cb ? -22 : -8, 0);
        if (strcmp(name, "Wi-Fi") == 0) lbl_net_state = value_label;
    }
    if (cb) {
        lv_obj_t *arrow = mk_label(row, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, C_INK2);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -7, 0);
        lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, NULL);
    } else {
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    }
    return row;
}

static void build_settings(void)
{
    scr_settings = lv_obj_create(NULL);
    plain(scr_settings);
    lv_obj_set_flex_flow(scr_settings, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *hdr = mk_box(scr_settings, LV_PCT(100), HEADER_H);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, C_LINE, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);

    lv_obj_t *back = mk_btn(hdr, LV_SYMBOL_LEFT, &lv_font_montserrat_14, false, on_close_settings, NULL);
    lv_obj_set_size(back, 48, HEADER_H - 1);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *t = mk_label(hdr, "设置", &font_cjk_18, C_INK);
    lv_obj_center(t);

    settings_body = mk_box(scr_settings, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_t *body = settings_body;
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    setting_nav_row(body, "Wi-Fi", "未连接", on_wifi_open);

    lv_obj_t *auto_row = mk_box(body, LV_PCT(100), ROW_H);
    lv_obj_set_style_border_side(auto_row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(auto_row, C_LINE, 0);
    lv_obj_set_style_border_width(auto_row, 1, 0);
    lv_obj_t *auto_label = mk_label(auto_row, "联网后自动续播", &font_cjk_14, C_INK);
    lv_obj_align(auto_label, LV_ALIGN_LEFT_MID, 8, 0);
    settings_auto = lv_switch_create(auto_row);
    lv_obj_set_size(settings_auto, 46, 28); lv_obj_align(settings_auto, LV_ALIGN_RIGHT_MID, -8, 0);
    if (s_autoplay) lv_obj_add_state(settings_auto, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(settings_auto, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(settings_auto, on_auto_change, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *time_row = mk_box(body, LV_PCT(100), 56);
    lv_obj_set_style_border_side(time_row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(time_row, C_LINE, 0);
    lv_obj_set_style_border_width(time_row, 1, 0);
    lv_obj_t *time_label = mk_label(time_row, "时间格式", &font_cjk_14, C_INK);
    lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_t *time_group = mk_box(time_row, 120, 44);
    lv_obj_set_style_bg_color(time_group, C_SURFACE, 0);
    lv_obj_set_style_radius(time_group, 10, 0);
    lv_obj_align(time_group, LV_ALIGN_RIGHT_MID, -8, 0);
    for (int i = 0; i < 2; i++) {
        settings_time_btn[i] = mk_btn(time_group, i == 0 ? "24 小时" : "12 小时",
                                      &font_cjk_14, false, on_time_format,
                                      (void *)(intptr_t)i);
        lv_obj_set_size(settings_time_btn[i], 60, 44);
        lv_obj_align(settings_time_btn[i], i == 0 ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, 0, 0);
    }
    refresh_time_format_buttons();

    lv_obj_t *theme_mode_row = mk_box(body, LV_PCT(100), 56);
    lv_obj_set_style_border_side(theme_mode_row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(theme_mode_row, C_LINE, 0);
    lv_obj_set_style_border_width(theme_mode_row, 1, 0);
    lv_obj_t *theme_mode_label = mk_label(theme_mode_row, "外观模式", &font_cjk_14, C_INK);
    lv_obj_align(theme_mode_label, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_t *theme_mode_group = mk_box(theme_mode_row, 174, 44);
    lv_obj_set_style_bg_color(theme_mode_group, C_SURFACE, 0);
    lv_obj_set_style_radius(theme_mode_group, 10, 0);
    lv_obj_align(theme_mode_group, LV_ALIGN_RIGHT_MID, -8, 0);
    static const char *mode_labels[] = {"亮色", "暗色", "自动"};
    for (int i = 0; i < 3; ++i) {
        settings_theme_btn[i] = mk_btn(theme_mode_group, mode_labels[i], &font_cjk_14,
                                       false, on_theme_mode, (void *)(intptr_t)i);
        lv_obj_set_size(settings_theme_btn[i], 58, 44);
        lv_obj_set_pos(settings_theme_btn[i], i * 58, 0);
    }

    settings_schedule_row = mk_box(body, LV_PCT(100), 52);
    lv_obj_set_style_border_side(settings_schedule_row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(settings_schedule_row, C_LINE, 0);
    lv_obj_set_style_border_width(settings_schedule_row, 1, 0);
    lv_obj_t *schedule_label = mk_label(settings_schedule_row, "暗色时段", &font_cjk_14, C_INK);
    lv_obj_align(schedule_label, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_t *schedule_group = mk_box(settings_schedule_row, 172, 40);
    lv_obj_set_flex_flow(schedule_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(schedule_group, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(schedule_group, LV_ALIGN_RIGHT_MID, -8, 0);
    settings_dark_start = lv_dropdown_create(schedule_group);
    lv_obj_set_size(settings_dark_start, 76, 40);
    style_hour_dropdown(settings_dark_start);
    lv_dropdown_set_selected(settings_dark_start, s_dark_start_hour);
    // 在透明区域里直接绘制，不依赖字体或超扁对象的 flex 尺寸解释。
    lv_obj_t *schedule_to = mk_box(schedule_group, 12, 16);
    lv_obj_set_style_bg_opa(schedule_to, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(schedule_to, draw_schedule_connector, LV_EVENT_DRAW_MAIN_END, NULL);
    settings_dark_end = lv_dropdown_create(schedule_group);
    lv_obj_set_size(settings_dark_end, 76, 40);
    style_hour_dropdown(settings_dark_end);
    lv_dropdown_set_selected(settings_dark_end, s_dark_end_hour);
    (void)schedule_to;

    lv_obj_t *accent_row = setting_nav_row(body, "主题色",
                                            theme_accent_name(s_theme_accent),
                                            on_theme_picker_open);
    lv_obj_set_height(accent_row, 52);
    settings_accent_dot = mk_box(accent_row, 18, 18);
    lv_obj_set_style_radius(settings_accent_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(settings_accent_dot, C_ACCENT, 0);
    lv_obj_align(settings_accent_dot, LV_ALIGN_RIGHT_MID, -88, 0);
    refresh_theme_mode_buttons();

    lv_obj_t *bright = mk_box(body, LV_PCT(100), ROW_H);
    lv_obj_set_style_border_side(bright, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bright, C_LINE, 0);
    lv_obj_set_style_border_width(bright, 1, 0);
    lv_obj_t *bl = mk_label(bright, "屏幕亮度", &font_cjk_14, C_INK); lv_obj_align(bl, LV_ALIGN_LEFT_MID, 8, 0);
    settings_brightness = lv_slider_create(bright);
    lv_obj_set_size(settings_brightness, 145, 4); lv_obj_align(settings_brightness, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_slider_set_range(settings_brightness, 10, 100);
    lv_slider_set_value(settings_brightness, s_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(settings_brightness, C_RAISED, LV_PART_MAIN);
    lv_obj_set_style_bg_color(settings_brightness, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(settings_brightness, C_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(settings_brightness, 7, LV_PART_KNOB);
    lv_obj_add_event_cb(settings_brightness, on_brightness_change, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(settings_brightness, on_brightness_release, LV_EVENT_RELEASED, NULL);

    setting_nav_row(body, "系统更新", ota_mgr_running_version(), on_ota_open);
    setting_nav_row(body, "诊断", NULL, on_diagnostics_open);
    setting_nav_row(body, "关于", NULL, on_about_open);
}

static void on_info_back(lv_event_t *e)
{
    (void)e;
    lv_obj_t *previous = lv_scr_act();
    lv_scr_load(scr_settings);
    release_info_screen(previous);
}

static lv_obj_t *build_page_header(lv_obj_t *screen, const char *title)
{
    lv_obj_t *hdr = mk_box(screen, LV_PCT(100), HEADER_H);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, C_LINE, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_t *back = mk_btn(hdr, LV_SYMBOL_LEFT, &lv_font_montserrat_14,
                            false, on_info_back, NULL);
    lv_obj_set_size(back, 48, HEADER_H - 1);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *label = mk_label(hdr, title, &font_cjk_18, C_INK);
    lv_obj_center(label);
    return hdr;
}

static void build_about(void)
{
    scr_about = lv_obj_create(NULL);
    plain(scr_about);
    lv_obj_set_flex_flow(scr_about, LV_FLEX_FLOW_COLUMN);
    build_page_header(scr_about, "关于");

    lv_obj_t *body = mk_box(scr_about, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(body, 1);

    lv_obj_t *identity = mk_box(body, 230, 52);
    lv_obj_align(identity, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_t *mark = mk_box(identity, 44, 44);
    lv_obj_set_style_bg_color(mark, C_TINT, 0);
    lv_obj_set_style_radius(mark, 13, 0);
    lv_obj_align(mark, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *mark_icon = mk_label(mark, LV_SYMBOL_AUDIO, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_center(mark_icon);
    lv_obj_t *brand = mk_label(identity, "Radio OS", &lv_font_montserrat_20, C_INK);
    lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 56, 2);
    lv_obj_t *tagline = mk_label(identity, "专注、纯粹的网络收音机", &font_cjk_14, C_INK2);
    lv_obj_align(tagline, LV_ALIGN_BOTTOM_LEFT, 56, -2);

    lv_obj_t *card = mk_box(body, 288, 92);
    lv_obj_set_style_bg_color(card, C_SURFACE, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_color(card, C_LINE, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_t *divider_v = mk_box(card, 1, 50);
    lv_obj_set_style_bg_color(divider_v, C_LINE, 0);
    lv_obj_align(divider_v, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *divider_h = mk_box(card, 288, 1);
    lv_obj_set_style_bg_color(divider_h, C_LINE, 0);
    lv_obj_align(divider_h, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t *vlabel = mk_label(card, "当前版本", &font_cjk_14, C_INK2);
    lv_obj_align(vlabel, LV_ALIGN_TOP_LEFT, 51, 6);
    lv_obj_t *version = mk_label(card, ota_mgr_running_version(), &lv_font_montserrat_14, C_INK);
    lv_obj_align(version, LV_ALIGN_TOP_LEFT, 55, 25);
    lv_obj_t *clabel = mk_label(card, "本机电台", &font_cjk_14, C_INK2);
    lv_obj_align(clabel, LV_ALIGN_TOP_RIGHT, -51, 6);
    char count[12];
    snprintf(count, sizeof(count), "%d", stations_catalog_count());
    lv_obj_t *catalog = mk_label(card, count, &lv_font_montserrat_14, C_INK);
    lv_obj_align(catalog, LV_ALIGN_TOP_RIGHT, -60, 25);
    lv_obj_t *by = mk_label(card, "设计与开发", &font_cjk_14, C_INK2);
    lv_obj_align(by, LV_ALIGN_BOTTOM_LEFT, 16, -11);
    lv_obj_t *author = mk_label(card, "BI8SYN", &lv_font_montserrat_14, C_ACCENT);
    lv_obj_align(author, LV_ALIGN_BOTTOM_RIGHT, -16, -11);
}

static void position_touch_object(lv_obj_t *object, lv_coord_t x, lv_coord_t y,
                                  lv_coord_t size)
{
    lv_obj_set_pos(object, x - size / 2, y - size / 2);
}

static void reset_touch_test(void)
{
    s_touch_step = 0;
    memset(s_touch_dx, 0, sizeof(s_touch_dx));
    memset(s_touch_dy, 0, sizeof(s_touch_dy));
    lv_label_set_text(touch_status, "第 1 / 5 点");
    lv_label_set_text(touch_detail, "请准确触摸紫色标记中心");
    position_touch_object(touch_target, s_touch_targets[0].x, s_touch_targets[0].y, 28);
    lv_obj_clear_flag(touch_target, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(touch_restart, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "TOUCH_TEST start");
}

static void on_touch_test_restart(lv_event_t *e)
{
    (void)e;
    reset_touch_test();
}

static void on_touch_test_sample(lv_event_t *e)
{
    (void)e;
    if (s_touch_step < 0 || s_touch_step >= 5) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);
    const lv_point_t expected = s_touch_targets[s_touch_step];
    int dx = point.x - expected.x;
    int dy = point.y - expected.y;
    s_touch_dx[s_touch_step] = dx;
    s_touch_dy[s_touch_step] = dy;
    position_touch_object(touch_dot, point.x, point.y, 8);
    lv_obj_clear_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG,
             "TOUCH_POINT point=%d/5 target=%d,%d actual=%d,%d delta=%+d,%+d",
             s_touch_step + 1, expected.x, expected.y, point.x, point.y, dx, dy);

    s_touch_step++;
    if (s_touch_step < 5) {
        char status[24];
        char detail[64];
        snprintf(status, sizeof(status), "第 %d / 5 点", s_touch_step + 1);
        snprintf(detail, sizeof(detail), "上点偏差 %+d, %+d · 继续触摸标记", dx, dy);
        lv_label_set_text(touch_status, status);
        lv_label_set_text(touch_detail, detail);
        position_touch_object(touch_target, s_touch_targets[s_touch_step].x,
                              s_touch_targets[s_touch_step].y, 28);
        return;
    }

    int sum_x = 0, sum_y = 0;
    int min_x = s_touch_dx[0], max_x = s_touch_dx[0];
    int min_y = s_touch_dy[0], max_y = s_touch_dy[0];
    for (int i = 0; i < 5; ++i) {
        sum_x += s_touch_dx[i];
        sum_y += s_touch_dy[i];
        if (s_touch_dx[i] < min_x) min_x = s_touch_dx[i];
        if (s_touch_dx[i] > max_x) max_x = s_touch_dx[i];
        if (s_touch_dy[i] < min_y) min_y = s_touch_dy[i];
        if (s_touch_dy[i] > max_y) max_y = s_touch_dy[i];
    }
    int mean_x = sum_x >= 0 ? (sum_x + 2) / 5 : (sum_x - 2) / 5;
    int mean_y = sum_y >= 0 ? (sum_y + 2) / 5 : (sum_y - 2) / 5;
    int spread_x = max_x - min_x;
    int spread_y = max_y - min_y;
    bool stable_offset = spread_x <= 6 && spread_y <= 6;
    char summary[112];
    snprintf(summary, sizeof(summary),
             "平均偏差 %+d, %+d · 离散 %d, %d\n%s",
             mean_x, mean_y, spread_x, spread_y,
             stable_offset ? "稳定偏移，建议驱动补偿" : "各处偏差不同，建议多点补偿");
    lv_label_set_text(touch_status, "测试完成");
    lv_label_set_text(touch_detail, summary);
    lv_obj_add_flag(touch_target, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(touch_restart, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG,
             "TOUCH_SUMMARY mean=%+d,%+d spread=%d,%d mode=%s",
             mean_x, mean_y, spread_x, spread_y,
             stable_offset ? "offset" : "multipoint");
}

static void on_touch_test_back(lv_event_t *e)
{
    (void)e;
    if (!scr_diagnostics) build_diagnostics_screen();
    refresh_diagnostics_screen();
    lv_scr_load(scr_diagnostics);
}

static void on_touch_test_open(lv_event_t *e)
{
    (void)e;
    reset_touch_test();
    lv_obj_t *previous = lv_scr_act();
    lv_scr_load(scr_touch_test);
    release_info_screen(previous);
}

static void build_touch_test(void)
{
    scr_touch_test = lv_obj_create(NULL);
    plain(scr_touch_test);

    lv_obj_t *header = mk_box(scr_touch_test, LV_PCT(100), HEADER_H);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, C_LINE, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_t *back = mk_btn(header, LV_SYMBOL_LEFT, &lv_font_montserrat_14,
                            false, on_touch_test_back, NULL);
    lv_obj_set_size(back, 48, HEADER_H - 1);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *title = mk_label(header, "触摸测试", &font_cjk_18, C_INK);
    lv_obj_center(title);

    lv_obj_t *capture = mk_box(scr_touch_test, LV_PCT(100), 240 - HEADER_H);
    lv_obj_set_pos(capture, 0, HEADER_H);
    lv_obj_add_flag(capture, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(capture, on_touch_test_sample, LV_EVENT_RELEASED, NULL);

    touch_status = mk_label(capture, "", &font_cjk_14, C_INK);
    lv_obj_set_width(touch_status, 300);
    lv_obj_set_style_text_align(touch_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(touch_status, LV_ALIGN_TOP_MID, 0, 4);
    touch_detail = mk_label(capture, "", &font_cjk_14, C_INK2);
    lv_obj_set_width(touch_detail, 304);
    lv_label_set_long_mode(touch_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(touch_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(touch_detail, LV_ALIGN_TOP_MID, 0, 24);

    touch_target = mk_box(scr_touch_test, 28, 28);
    lv_obj_set_style_bg_opa(touch_target, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch_target, 2, 0);
    lv_obj_set_style_border_color(touch_target, C_ACCENT, 0);
    lv_obj_set_style_radius(touch_target, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *target_h = mk_box(touch_target, 20, 2);
    lv_obj_set_style_bg_color(target_h, C_ACCENT, 0);
    lv_obj_center(target_h);
    lv_obj_t *target_v = mk_box(touch_target, 2, 20);
    lv_obj_set_style_bg_color(target_v, C_ACCENT, 0);
    lv_obj_center(target_v);
    lv_obj_clear_flag(touch_target, LV_OBJ_FLAG_CLICKABLE);

    touch_dot = mk_box(scr_touch_test, 8, 8);
    lv_obj_set_style_bg_color(touch_dot, C_ACCENT, 0);
    lv_obj_set_style_radius(touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(touch_dot, LV_OBJ_FLAG_CLICKABLE);

    touch_restart = mk_btn(capture, "重新测试", &font_cjk_14, true,
                           on_touch_test_restart, NULL);
    lv_obj_set_size(touch_restart, 128, 44);
    lv_obj_align(touch_restart, LV_ALIGN_BOTTOM_MID, 0, -16);
    reset_touch_test();
}

static void build_diagnostics_screen(void)
{
    scr_diagnostics = lv_obj_create(NULL);
    plain(scr_diagnostics);
    lv_obj_set_flex_flow(scr_diagnostics, LV_FLEX_FLOW_COLUMN);
    build_page_header(scr_diagnostics, "诊断");
    diagnostics_body = mk_box(scr_diagnostics, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(diagnostics_body, 1);
    lv_obj_set_flex_flow(diagnostics_body, LV_FLEX_FLOW_COLUMN);
    refresh_diagnostics_screen();
}

static void refresh_diagnostics_screen(void)
{
    if (!diagnostics_body) return;
    lv_obj_clean(diagnostics_body);
    char mem[32];
    snprintf(mem, sizeof(mem), "%u KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    setting_nav_row(diagnostics_body, "网络", s_wifi_ok ? "已连接" : "未连接", NULL);
    setting_nav_row(diagnostics_body, "音频", board_codec() ? "正常" : "不可用", NULL);
    char catalog[16]; snprintf(catalog, sizeof(catalog), "%d", stations_catalog_count());
    setting_nav_row(diagnostics_body, "电台目录", catalog, NULL);
    setting_nav_row(diagnostics_body, "内部内存", mem, NULL);
    setting_nav_row(diagnostics_body, "触摸测试", "五点检测", on_touch_test_open);
}

static void ota_render_session(void);

static void on_ota_status(const ota_mgr_status_t *status, void *user_data)
{
    (void)status;
    (void)user_data;
    if (lvgl_port_lock(500)) {
        refresh_ota_banner();
        ota_render_session();
        lvgl_port_unlock();
    }
}

static void on_ota_start(lv_event_t *e)
{
    (void)e;
    if (!s_wifi_ok) {
        ota_mgr_status_t status = {
            .state = OTA_MGR_ERROR,
            .progress = 0,
        };
        snprintf(status.message, sizeof(status.message), "请先连接 Wi-Fi");
        // 页面直接重绘离线状态；联网后再次点击即可。
        lv_obj_clean(ota_body);
        lv_obj_t *message = mk_label(ota_body, status.message, &font_cjk_18, C_INK);
        lv_obj_align(message, LV_ALIGN_CENTER, 0, -8);
        lv_obj_t *hint = mk_label(ota_body, "返回设置连接网络后重试", &font_cjk_14, C_INK2);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 22);
        return;
    }
    ota_mgr_status_t current = { 0 };
    ota_mgr_status(&current);
    bool install = current.state == OTA_MGR_UPDATE_AVAILABLE;
    // 检查更新不打断收听，只有用户确认安装时才释放播放器资源。
    if (install) player_stop();
    esp_err_t err = install ? ota_mgr_start(on_ota_status, NULL)
                            : ota_mgr_check(on_ota_status, NULL);
    if (err != ESP_OK) ota_render_session();
}

static void ota_render_session(void)
{
    if (!ota_body) return;
    lv_obj_clean(ota_body);
    ota_mgr_status_t status = { 0 };
    ota_mgr_status(&status);
    bool busy = status.state == OTA_MGR_CHECKING || status.state == OTA_MGR_DOWNLOADING;

    lv_obj_t *icon_box = mk_box(ota_body, 40, 40);
    lv_obj_set_style_bg_color(icon_box, C_TINT, 0);
    lv_obj_set_style_radius(icon_box, 12, 0);
    lv_obj_align(icon_box, LV_ALIGN_TOP_LEFT, 28, 10);
    const char *icon_text = status.state == OTA_MGR_UP_TO_DATE ||
                            status.state == OTA_MGR_READY_TO_RESTART
                            ? LV_SYMBOL_OK : (busy ? LV_SYMBOL_REFRESH : LV_SYMBOL_DOWNLOAD);
    lv_obj_t *icon = mk_label(icon_box, icon_text, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_center(icon);

    const char *title_text = "在线更新";
    if (status.state == OTA_MGR_CHECKING) title_text = "正在检查更新";
    else if (status.state == OTA_MGR_UPDATE_AVAILABLE) title_text = "发现新版本";
    else if (status.state == OTA_MGR_DOWNLOADING) title_text = "正在下载更新";
    else if (status.state == OTA_MGR_UP_TO_DATE) title_text = "已是最新版本";
    else if (status.state == OTA_MGR_READY_TO_RESTART) title_text = "更新完成";
    else if (status.state == OTA_MGR_UNAVAILABLE || status.state == OTA_MGR_ERROR)
        title_text = "暂时无法更新";
    lv_obj_t *title = mk_label(ota_body, title_text, &font_cjk_18, C_INK);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 84, 8);

    char version_text[72];
    if (status.state == OTA_MGR_UPDATE_AVAILABLE)
        snprintf(version_text, sizeof(version_text), "当前 %s  ·  最新 %s",
                 ota_mgr_running_version(), status.available_version);
    else
        snprintf(version_text, sizeof(version_text), "当前版本  %s", ota_mgr_running_version());
    lv_obj_t *version = mk_label(ota_body, version_text, &font_cjk_14, C_INK2);
    lv_obj_align(version, LV_ALIGN_TOP_LEFT, 84, 32);

    const char *copy_text = status.message[0] ? status.message :
                            "通过 Wi-Fi 安全下载并校验新固件";
    lv_obj_t *copy = mk_label(ota_body, copy_text, &font_cjk_14, C_INK2);
    lv_obj_set_width(copy, 288);
    lv_obj_set_style_text_align(copy, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(copy, LV_ALIGN_TOP_MID, 0, 58);

    if (status.state == OTA_MGR_UPDATE_AVAILABLE && status.release_notes[0]) {
        lv_obj_t *notes_title = mk_label(ota_body, "更新内容", &font_cjk_14, C_INK);
        lv_obj_set_pos(notes_title, 16, 82);
        lv_obj_t *notes = mk_label(ota_body, status.release_notes, &font_cjk_14, C_INK2);
        lv_obj_set_size(notes, 288, 54);
        lv_obj_set_pos(notes, 16, 104);
        lv_label_set_long_mode(notes, LV_LABEL_LONG_WRAP);
    }

    if (status.state == OTA_MGR_DOWNLOADING) {
        lv_obj_t *bar = lv_bar_create(ota_body);
        lv_obj_set_size(bar, 248, 5);
        lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 105);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, status.progress, LV_ANIM_ON);
        lv_obj_set_style_bg_color(bar, C_RAISED, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, C_ACCENT, LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    } else if (!busy && status.state != OTA_MGR_READY_TO_RESTART) {
        const char *button_text = status.state == OTA_MGR_UPDATE_AVAILABLE ? "立即更新" : "检查更新";
        lv_obj_t *check = mk_btn(ota_body, button_text, &font_cjk_14, true, on_ota_start, NULL);
        lv_obj_set_size(check, 160, 40);
        lv_obj_align(check, LV_ALIGN_TOP_MID, 0,
                     status.state == OTA_MGR_UPDATE_AVAILABLE ? 146 : 102);
    }
}

static void on_ota_open(lv_event_t *e)
{
    (void)e;
    if (!scr_ota) build_ota_screen();
    lv_scr_load(scr_ota);
    ota_render_session();
}

static void build_ota_screen(void)
{
    scr_ota = lv_obj_create(NULL);
    plain(scr_ota);
    lv_obj_set_flex_flow(scr_ota, LV_FLEX_FLOW_COLUMN);
    build_page_header(scr_ota, "系统更新");
    ota_body = mk_box(scr_ota, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(ota_body, 1);
    ota_render_session();
}

static void on_open_settings(lv_event_t *e)
{
    (void)e;
    char ssid[33];
    wifi_mgr_current_ssid(ssid, sizeof(ssid));
    lv_label_set_text(lbl_net_state, s_wifi_ok && ssid[0] ? ssid : "未连接");
    lv_scr_load(scr_settings);
}

static void on_drawer_close(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(drawer_layer, LV_OBJ_FLAG_HIDDEN);
}

static void on_drawer_open(lv_event_t *e)
{
    (void)e;
    update_sleep_label();
    lv_obj_clear_flag(drawer_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(drawer_layer);
}

static void on_drawer_settings(lv_event_t *e)
{
    on_drawer_close(e);
    on_open_settings(e);
}

static void on_donation_close(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(donation_layer, LV_OBJ_FLAG_HIDDEN);
}

static void on_donation_open(lv_event_t *e)
{
    on_drawer_close(e);
    lv_obj_clear_flag(donation_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(donation_layer);
}

static lv_obj_t *build_coffee_icon(lv_obj_t *parent)
{
    lv_obj_t *icon = mk_box(parent, 20, 20);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_t *cup = mk_box(icon, 13, 10);
    lv_obj_set_pos(cup, 1, 4);
    lv_obj_set_style_bg_opa(cup, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(cup, C_INK2, 0);
    lv_obj_set_style_border_width(cup, 2, 0);
    lv_obj_set_style_border_side(cup, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT |
                                      LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(cup, 4, 0);
    lv_obj_t *handle = mk_box(icon, 7, 7);
    lv_obj_set_pos(handle, 12, 6);
    lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(handle, C_INK2, 0);
    lv_obj_set_style_border_width(handle, 2, 0);
    lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *saucer = mk_box(icon, 18, 2);
    lv_obj_set_pos(saucer, 1, 16);
    lv_obj_set_style_bg_color(saucer, C_INK2, 0);
    lv_obj_set_style_radius(saucer, 1, 0);
    return icon;
}

typedef enum {
    SMALL_ICON_DISPLAY_OFF,
    SMALL_ICON_TIMER,
} small_icon_t;

static void draw_small_icon(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    small_icon_t kind = (small_icon_t)(intptr_t)lv_event_get_user_data(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    lv_color_t color = kind == SMALL_ICON_TIMER ? C_ACCENT : C_INK2;
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_TRANSP;
    rect.border_color = color;
    rect.border_width = kind == SMALL_ICON_TIMER ? 1 : 2;
    rect.radius = kind == SMALL_ICON_TIMER ? LV_RADIUS_CIRCLE : 3;
    lv_area_t outline = kind == SMALL_ICON_TIMER
        ? (lv_area_t){area.x1, area.y1, area.x2, area.y2}
        : (lv_area_t){area.x1 + 1, area.y1 + 2, area.x2 - 2, area.y2 - 6};
    lv_draw_rect(ctx, &rect, &outline);

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = kind == SMALL_ICON_TIMER ? 1 : 2;
    line.round_start = true;
    line.round_end = true;
    if (kind == SMALL_ICON_TIMER) {
        lv_coord_t cx = (area.x1 + area.x2) / 2;
        lv_coord_t cy = (area.y1 + area.y2) / 2;
        lv_point_t hour[2] = {{cx, cy}, {cx, cy - 4}};
        lv_point_t minute[2] = {{cx, cy}, {cx + 4, cy}};
        lv_draw_line(ctx, &line, &hour[0], &hour[1]);
        lv_draw_line(ctx, &line, &minute[0], &minute[1]);
    } else {
        lv_coord_t cx = (area.x1 + area.x2) / 2;
        lv_point_t stand[2] = {{cx - 4, area.y2 - 2}, {cx + 4, area.y2 - 2}};
        lv_draw_line(ctx, &line, &stand[0], &stand[1]);
    }
}

static lv_obj_t *build_small_icon(lv_obj_t *parent, small_icon_t kind)
{
    lv_obj_t *icon = mk_box(parent, kind == SMALL_ICON_TIMER ? 14 : 20,
                           kind == SMALL_ICON_TIMER ? 14 : 20);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(icon, draw_small_icon, LV_EVENT_DRAW_MAIN_END,
                        (void *)(intptr_t)kind);
    return icon;
}

static void build_screen_wake_layer(void)
{
    screen_wake_layer = mk_box(lv_layer_top(), 320, 240);
    lv_obj_set_pos(screen_wake_layer, 0, 0);
    lv_obj_set_style_bg_opa(screen_wake_layer, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(screen_wake_layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(screen_wake_layer, on_screen_wake, LV_EVENT_PRESSED, NULL);
}

static void build_donation_dialog(void)
{
    donation_layer = mk_box(scr_player, 320, 240);
    lv_obj_set_style_bg_color(donation_layer, C_INK, 0);
    lv_obj_set_style_bg_opa(donation_layer, 84, 0);
    lv_obj_add_flag(donation_layer, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE |
                                    LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(donation_layer, 0, 0);
    lv_obj_add_event_cb(donation_layer, on_donation_close, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = mk_box(donation_layer, 216, 232);
    lv_obj_set_pos(card, 52, 4);
    lv_obj_set_style_bg_color(card, C_WHITE, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_t *title = mk_label(card, "请我喝杯咖啡", &font_cjk_14, C_INK);
    lv_obj_set_pos(title, 16, 13);
    lv_obj_t *close = mk_btn(card, LV_SYMBOL_CLOSE, &lv_font_montserrat_14,
                             false, on_donation_close, NULL);
    lv_obj_set_size(close, 44, 40);
    lv_obj_set_style_bg_opa(close, LV_OPA_TRANSP, 0);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *qr = lv_img_create(card);
    lv_img_set_src(qr, &donation_qr);
    lv_obj_set_pos(qr, 20, 44);
}

static void render_audio_visual(const int bands[PLAYER_SPECTRUM_BANDS], int level)
{
    lv_obj_t *canvas = visual_canvas;
    if (!canvas) return;
    const int width = 236;
    const int height = 44;
    lv_canvas_fill_bg(canvas, LV_COLOR_CHROMA_KEY, LV_OPA_COVER);

    const int bar_w = 6;
    const int gap = 5;
    const int max_h = 34;
    const int total_w = 17 * bar_w + 16 * gap;
    const int start_x = (width - total_w) / 2;
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.radius = 3;

    // 中心不是 FFT 的最低频 bin：最低频很容易接近零，会让视觉锚点塌陷。
    // 用 RMS 响度和低中频能量共同驱动中心，再让真实频谱只负责细节。
    int low_mid = (bands[0] * 3 + bands[1] * 2 + bands[2] * 2 + bands[3]) / 8;
    int energy = (level * 3 + low_mid * 2) / 5;
    bool active = energy > 0;
    if (!active) {
        for (int band = 0; band < PLAYER_SPECTRUM_BANDS; ++band) {
            if (bands[band] > 0) { active = true; break; }
        }
    }

    static const uint8_t envelope[PLAYER_SPECTRUM_BANDS] = {
        100, 91, 82, 73, 64, 55, 46, 37, 28,
    };
    int targets[PLAYER_SPECTRUM_BANDS] = {0};
    targets[0] = active ? 28 + energy * 72 / 100 : 0;
    for (int distance = 1; distance < PLAYER_SPECTRUM_BANDS; ++distance) {
        int band = distance - 1;
        int spectral = bands[band];
        if (distance == PLAYER_SPECTRUM_BANDS - 1) {
            spectral = (bands[7] + bands[8]) / 2;
        }
        // 82% 稳定轮廓 + 18% 真实频段起伏。固定空间包络确保视线
        // 从中心自然向外衰减，同时仍能看出音乐频谱的变化。
        int shaped = (targets[0] * 82 + spectral * 18) / 100;
        targets[distance] = shaped * envelope[distance] / 100;
        int max_allowed = targets[distance - 1] > 2 ? targets[distance - 1] - 2 : 0;
        if (targets[distance] > max_allowed) targets[distance] = max_allowed;
    }

    for (int distance = 0; distance < PLAYER_SPECTRUM_BANDS; ++distance) {
        int current = s_visual_levels[distance];
        int target = targets[distance];
        s_visual_levels[distance] = target > current ? (current + target * 2) / 3
                                                      : (current * 7 + target) / 8;
    }

    for (int i = 0; i < 17; i++) {
        int distance = i < 8 ? 8 - i : i - 8;
        int value = s_visual_levels[distance];
        int bar_h = 3 + value * (max_h - 3) / 100;
        int accent_mix = 215 - distance * 14 + value * 40 / 100;
        if (accent_mix > 255) accent_mix = 255;
        rect.bg_color = lv_color_mix(C_ACCENT, C_PAPER, accent_mix);
        lv_canvas_draw_rect(canvas, start_x + i * (bar_w + gap),
                            (height - bar_h) / 2, bar_w, bar_h, &rect);
    }
}

static void anim_set_opa(void *object, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)value, 0);
}

static void anim_set_y(void *object, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)object, (lv_coord_t)value);
}

static void anim_set_width(void *object, int32_t value)
{
    lv_obj_set_width((lv_obj_t *)object, (lv_coord_t)value);
    lv_obj_align((lv_obj_t *)object, LV_ALIGN_TOP_MID, 0, 181);
}

static void anim_set_boot_wave(void *object, int32_t value)
{
    s_boot_wave = value;
    lv_obj_invalidate((lv_obj_t *)object);
}

static void draw_boot_mark(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const lv_coord_t cx = (area.x1 + area.x2) / 2;
    const lv_coord_t cy = (area.y1 + area.y2) / 2;
    static const int8_t final_heights[] = {12, 24, 34, 24, 12};
    lv_draw_rect_dsc_t bar;
    lv_draw_rect_dsc_init(&bar);
    bar.bg_color = C_ACCENT;
    bar.radius = LV_RADIUS_CIRCLE;
    for (int i = 0; i < 5; ++i) {
        int delayed = s_boot_wave - i * 8;
        if (delayed < 0) delayed = 0;
        if (delayed > 100) delayed = 100;
        lv_coord_t height = 3 + (final_heights[i] - 3) * delayed / 100;
        lv_area_t rect = {
            cx - 15 + i * 7, cy - height / 2,
            cx - 12 + i * 7, cy + height / 2,
        };
        lv_draw_rect(ctx, &bar, &rect);
    }
}

static void start_boot_anim(lv_obj_t *object, lv_anim_exec_xcb_t exec,
                            int from, int to, int duration, int delay)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_time(&animation, duration);
    lv_anim_set_delay(&animation, delay);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation, exec);
    lv_anim_start(&animation);
}

static void boot_finish_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!scr_boot) return;
    lv_obj_t *boot = scr_boot;
    scr_boot = NULL;
    if (lv_scr_act() == boot) {
        refresh_player();
        refresh_wifi();
        lv_scr_load_anim(scr_player, LV_SCR_LOAD_ANIM_FADE_ON, BOOT_FADE_MS, 0, true);
        lv_timer_t *done = lv_timer_create(boot_transition_done_cb, BOOT_FADE_MS + 40, NULL);
        lv_timer_set_repeat_count(done, 1);
    } else {
        lv_obj_del(boot);
        s_boot_ready = true;
        if (s_theme_rebuild_pending) request_theme_rebuild();
    }
}

static void boot_transition_done_cb(lv_timer_t *timer)
{
    (void)timer;
    s_boot_ready = true;
    if (s_theme_rebuild_pending) request_theme_rebuild();
}

static void build_boot_screen(void)
{
    scr_boot = lv_obj_create(NULL);
    plain(scr_boot);

    lv_obj_t *mark = mk_box(scr_boot, 56, 56);
    lv_obj_set_pos(mark, 132, 56);
    lv_obj_set_style_radius(mark, 18, 0);
    lv_obj_set_style_bg_color(mark, C_TINT, 0);
    lv_obj_set_style_opa(mark, LV_OPA_0, 0);
    lv_obj_add_event_cb(mark, draw_boot_mark, LV_EVENT_DRAW_MAIN_END, NULL);

    lv_obj_t *title = mk_label(scr_boot, "Radio OS", &lv_font_montserrat_20, C_INK);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 126);
    lv_obj_set_style_opa(title, LV_OPA_0, 0);

    lv_obj_t *signature = mk_label(scr_boot, "powered by BI8SYN",
                                   &lv_font_montserrat_12, C_INK2);
    lv_obj_set_style_text_letter_space(signature, 1, 0);
    lv_obj_align(signature, LV_ALIGN_TOP_MID, 0, 154);
    lv_obj_set_style_opa(signature, LV_OPA_0, 0);

    lv_obj_t *line = mk_box(scr_boot, 1, 2);
    lv_obj_set_style_radius(line, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(line, C_ACCENT, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 181);

    s_boot_wave = 0;
    start_boot_anim(mark, anim_set_opa, LV_OPA_0, LV_OPA_COVER, 380, 80);
    start_boot_anim(mark, anim_set_y, 66, 56, 520, 80);
    start_boot_anim(mark, anim_set_boot_wave, 0, 100, 620, 140);
    start_boot_anim(title, anim_set_opa, LV_OPA_0, LV_OPA_COVER, 420, 360);
    start_boot_anim(title, anim_set_y, 136, 126, 500, 320);
    start_boot_anim(signature, anim_set_opa, LV_OPA_0, LV_OPA_COVER, 420, 620);
    start_boot_anim(signature, anim_set_y, 163, 154, 500, 580);
    start_boot_anim(line, anim_set_width, 1, 72, 600, 720);
}

static void ui_live_timer(lv_timer_t *timer)
{
    (void)timer;
    if (s_sleep_expired) {
        s_sleep_expired = false;
        s_sleep_minutes = 0;
        s_sleep_deadline_us = 0;
        player_stop();
        refresh_player();
        update_sleep_label();
        update_sleep_countdown();
        set_display_awake_locked(false);
    }
    player_metrics_t m = { 0 };
    player_metrics(&m);
    ++s_live_ticks;
    if (lv_scr_act() == scr_player) render_audio_visual(m.bands, m.level);

    if (s_live_ticks % 25 == 0 && lbl_clock) {
        time_t now = time(NULL);
        struct tm local;
        if (now > 1700000000 && localtime_r(&now, &local)) {
            char clock[20];
            if (s_time_24h) strftime(clock, sizeof(clock), "%H:%M", &local);
            else snprintf(clock, sizeof(clock), "%s %d:%02d",
                          local.tm_hour < 12 ? "上午" : "下午",
                          local.tm_hour % 12 ? local.tm_hour % 12 : 12, local.tm_min);
            lv_label_set_text(lbl_clock, clock);
        } else {
            lv_label_set_text(lbl_clock, "--:--");
        }
        update_sleep_countdown();
        if (s_theme_mode == THEME_MODE_AUTO) apply_theme_choice(false);
        if (s_theme_rebuild_pending) request_theme_rebuild();
    }
}

static void build_drawer(void)
{
    drawer_layer = mk_box(scr_player, 320, 240);
    lv_obj_set_style_bg_opa(drawer_layer, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(drawer_layer, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(drawer_layer, 0, 0);
    lv_obj_add_flag(drawer_layer, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *drawer = mk_box(drawer_layer, 214, 240);
    lv_obj_set_pos(drawer, 0, 0);
    lv_obj_set_style_bg_color(drawer, C_PAPER, 0);
    lv_obj_set_style_border_side(drawer, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(drawer, C_LINE, 0);
    lv_obj_set_style_border_width(drawer, 1, 0);
    lv_obj_set_style_shadow_width(drawer, 0, 0);

    lv_obj_t *head = mk_box(drawer, 214, 48);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(head, C_LINE, 0);
    lv_obj_set_style_border_width(head, 1, 0);
    lv_obj_t *brand = mk_label(head, "Radio OS", &lv_font_montserrat_18, C_INK);
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *author = mk_label(head, "by BI8SYN", &lv_font_montserrat_12, C_INK2);
    lv_obj_align_to(author, brand, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -1);
    lv_obj_t *close = mk_btn(head, LV_SYMBOL_CLOSE, &lv_font_montserrat_14, false,
                             on_drawer_close, NULL);
    lv_obj_set_size(close, 42, 46);
    lv_obj_set_style_bg_opa(close, LV_OPA_TRANSP, 0);
    lv_obj_align(close, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *quick = mk_box(drawer, 214, 60);
    lv_obj_set_pos(quick, 0, 48);

    lv_obj_t *screen_off = lv_btn_create(quick);
    lv_obj_set_size(screen_off, 99, 44);
    lv_obj_set_pos(screen_off, 6, 8);
    lv_obj_set_style_radius(screen_off, 10, 0);
    lv_obj_set_style_border_width(screen_off, 0, 0);
    lv_obj_set_style_bg_color(screen_off, C_SURFACE, 0);
    lv_obj_set_style_bg_color(screen_off, C_RAISED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(screen_off, 0, 0);
    lv_obj_set_style_pad_all(screen_off, 0, 0);
    lv_obj_add_event_cb(screen_off, on_screen_off, LV_EVENT_CLICKED, NULL);
    lv_obj_t *display_icon = build_small_icon(screen_off, SMALL_ICON_DISPLAY_OFF);
    lv_obj_set_pos(display_icon, 9, 12);
    lv_obj_t *screen_name = mk_label(screen_off, "息屏播放", &font_cjk_14, C_INK);
    lv_obj_set_pos(screen_name, 34, 13);

    lv_obj_t *sleep = lv_btn_create(quick);
    lv_obj_set_size(sleep, 99, 44);
    lv_obj_set_pos(sleep, 109, 8);
    lv_obj_set_style_radius(sleep, 10, 0);
    lv_obj_set_style_border_width(sleep, 0, 0);
    lv_obj_set_style_bg_color(sleep, C_SURFACE, 0);
    lv_obj_set_style_bg_color(sleep, C_RAISED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(sleep, 0, 0);
    lv_obj_set_style_pad_all(sleep, 0, 0);
    lv_obj_add_event_cb(sleep, on_sleep_cycle, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sleep_icon = mk_label(sleep, LV_SYMBOL_BELL, &lv_font_montserrat_14, C_INK2);
    lv_obj_set_pos(sleep_icon, 9, 15);
    drawer_sleep_label = mk_label(sleep, "睡眠关闭", &font_cjk_14, C_INK);
    lv_obj_set_pos(drawer_sleep_label, 31, 13);

    lv_obj_t *settings = lv_btn_create(drawer);
    lv_obj_set_size(settings, 214, 45);
    lv_obj_set_pos(settings, 0, 108);
    lv_obj_set_style_radius(settings, 0, 0);
    lv_obj_set_style_border_width(settings, 0, 0);
    lv_obj_set_style_border_side(settings, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(settings, C_LINE, 0);
    lv_obj_set_style_border_width(settings, 1, 0);
    lv_obj_set_style_bg_color(settings, C_PAPER, 0);
    lv_obj_set_style_bg_color(settings, C_TINT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(settings, 0, 0);
    lv_obj_set_style_pad_all(settings, 0, 0);
    lv_obj_add_event_cb(settings, on_drawer_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t *settings_icon = mk_label(settings, LV_SYMBOL_SETTINGS, &lv_font_montserrat_14, C_INK2);
    lv_obj_set_pos(settings_icon, 13, 15);
    lv_obj_t *settings_name = mk_label(settings, "设置", &font_cjk_14, C_INK);
    lv_obj_set_pos(settings_name, 42, 13);
    lv_obj_t *settings_arrow = mk_label(settings, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, C_INK2);
    lv_obj_align(settings_arrow, LV_ALIGN_RIGHT_MID, -6, 0);

    lv_obj_t *donate = lv_btn_create(drawer);
    lv_obj_set_size(donate, 214, 45);
    lv_obj_set_pos(donate, 0, 153);
    lv_obj_set_style_radius(donate, 0, 0);
    lv_obj_set_style_border_width(donate, 0, 0);
    lv_obj_set_style_border_side(donate, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(donate, C_LINE, 0);
    lv_obj_set_style_border_width(donate, 1, 0);
    lv_obj_set_style_bg_color(donate, C_PAPER, 0);
    lv_obj_set_style_bg_color(donate, C_TINT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(donate, 0, 0);
    lv_obj_set_style_pad_all(donate, 0, 0);
    lv_obj_add_event_cb(donate, on_donation_open, LV_EVENT_CLICKED, NULL);
    lv_obj_t *coffee_icon = build_coffee_icon(donate);
    lv_obj_set_pos(coffee_icon, 12, 12);
    lv_obj_t *donate_name = mk_label(donate, "请我喝杯咖啡", &font_cjk_14, C_INK);
    lv_obj_set_pos(donate_name, 42, 13);
    lv_obj_t *donate_arrow = mk_label(donate, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, C_INK2);
    lv_obj_align(donate_arrow, LV_ALIGN_RIGHT_MID, -6, 0);

    lv_obj_t *footer = mk_box(drawer, 214, 32);
    lv_obj_set_pos(footer, 0, 208);
    char footer_text[48];
    snprintf(footer_text, sizeof(footer_text), "Radio OS %s", ota_mgr_running_version());
    lv_obj_t *note = mk_label(footer, footer_text, &lv_font_montserrat_12, C_INK2);
    lv_obj_center(note);

    lv_obj_t *scrim = mk_box(drawer_layer, 106, 240);
    lv_obj_set_pos(scrim, 214, 0);
    lv_obj_set_style_bg_color(scrim, C_INK, 0);
    lv_obj_set_style_bg_opa(scrim, 72, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scrim, on_drawer_close, LV_EVENT_CLICKED, NULL);
    update_sleep_label();
}

static void build_player(void)
{
    scr_player = lv_obj_create(NULL);
    plain(scr_player);

    lv_obj_t *top = mk_box(scr_player, 320, 40);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_t *menu = mk_btn(top, LV_SYMBOL_BARS, &lv_font_montserrat_20, false,
                            on_drawer_open, NULL);
    lv_obj_set_size(menu, 44, 36);
    lv_obj_set_pos(menu, 6, 2);
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, 0);
    lv_obj_t *top_center = mk_box(top, 210, 36);
    lv_obj_set_pos(top_center, 55, 2);
    lv_obj_set_flex_flow(top_center, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top_center, 7, 0);
    lbl_clock = mk_label(top_center, "--:--", &font_cjk_14, C_INK2);
    top_sleep_group = mk_box(top_center, 68, 18);
    lv_obj_set_flex_flow(top_sleep_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_sleep_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top_sleep_group, 4, 0);
    build_small_icon(top_sleep_group, SMALL_ICON_TIMER);
    lbl_sleep_countdown = mk_label(top_sleep_group, "15:00", &lv_font_montserrat_14, C_ACCENT);
    lv_obj_add_flag(top_sleep_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *wifi_btn = mk_btn(top, LV_SYMBOL_WIFI, &lv_font_montserrat_20, false,
                                on_wifi_open, NULL);
    lv_obj_set_size(wifi_btn, 44, 36);
    lv_obj_set_pos(wifi_btn, 270, 2);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_TRANSP, 0);
    lbl_wifi = lv_obj_get_child(wifi_btn, 0);
    lbl_offline = NULL;

    lv_obj_t *title = mk_box(scr_player, 320, 66);
    lv_obj_set_pos(title, 0, 40);
    lbl_meta = mk_label(title, "", &font_cjk_14, C_INK2);
    lv_obj_set_width(lbl_meta, 288);
    lv_obj_set_style_text_align(lbl_meta, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_meta, 16, 6);
    lbl_name = mk_label(title, "未选择电台", &font_cjk_24, C_INK);
    lv_obj_set_size(lbl_name, 288, 31);
    lv_obj_set_pos(lbl_name, 16, 27);
    lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(lbl_name, LV_TEXT_ALIGN_CENTER, 0);

    home_visual = build_audio_visual(scr_player, 236, 44);
    lv_obj_set_pos(home_visual, 42, 106);

    lbl_state = mk_label(scr_player, "已暂停", &font_cjk_14, C_INK2);
    lv_obj_set_size(lbl_state, 304, 18);
    lv_obj_set_pos(lbl_state, 8, 150);
    lv_obj_set_style_text_align(lbl_state, LV_TEXT_ALIGN_CENTER, 0);

    home_update_banner = mk_box(scr_player, 304, 22);
    lv_obj_set_pos(home_update_banner, 8, 146);
    lv_obj_set_style_radius(home_update_banner, 8, 0);
    lv_obj_set_style_bg_color(home_update_banner, C_TINT, 0);
    lv_obj_set_style_bg_opa(home_update_banner, LV_OPA_COVER, 0);
    lv_obj_add_flag(home_update_banner, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_ext_click_area(home_update_banner, 10);
    lv_obj_add_event_cb(home_update_banner, on_ota_open, LV_EVENT_CLICKED, NULL);
    home_update_label = mk_label(home_update_banner, "发现新版本", &font_cjk_14, C_ACCENT);
    lv_obj_center(home_update_label);
    // 不使用字体私有区或不完整标点字形，避免 OTA 横幅尾部出现豆腐块。
    static const lv_point_t update_chevron_points[] = {{0, 0}, {4, 4}, {0, 8}};
    lv_obj_t *update_chevron = lv_line_create(home_update_banner);
    lv_line_set_points(update_chevron, update_chevron_points, 3);
    lv_obj_set_size(update_chevron, 5, 9);
    lv_obj_set_style_line_color(update_chevron, C_ACCENT, 0);
    lv_obj_set_style_line_width(update_chevron, 2, 0);
    lv_obj_set_style_line_rounded(update_chevron, true, 0);
    lv_obj_align(update_chevron, LV_ALIGN_RIGHT_MID, -12, 0);

    row_err = mk_box(scr_player, 320, 46);
    lv_obj_set_pos(row_err, 0, 168);
    lv_obj_set_flex_flow(row_err, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_err, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row_err, 8, 0);
    lv_obj_add_flag(row_err, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *b_retry = mk_btn(row_err, "重试", &font_cjk_14, true, on_retry_play, NULL);
    lv_obj_set_size(b_retry, 104, 40);
    lv_obj_t *b_nextx = mk_btn(row_err, "下一台", &font_cjk_14, false, on_next, NULL);
    lv_obj_set_size(b_nextx, 104, 40);

    player_controls = mk_box(scr_player, 320, 46);
    lv_obj_set_pos(player_controls, 0, 168);
    lv_obj_set_flex_flow(player_controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(player_controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(player_controls, 6, 0);
    lv_obj_t *buttons[5];
    buttons[0] = mk_player_icon_btn(player_controls, PLAYER_ICON_LIST, on_goto_list);
    buttons[1] = mk_player_icon_btn(player_controls, PLAYER_ICON_PREV, on_prev);
    buttons[2] = mk_player_icon_btn(player_controls, PLAYER_ICON_PLAY, on_toggle_play);
    buttons[3] = mk_player_icon_btn(player_controls, PLAYER_ICON_NEXT, on_next);
    buttons[4] = mk_favorite_btn(player_controls, false, on_player_fav, NULL);
    for (int i = 0; i < 5; i++) {
        lv_obj_set_size(buttons[i], i == 2 ? 66 : 54, 44);
        lv_obj_set_style_bg_opa(buttons[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(buttons[i], LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(buttons[i], C_TINT, LV_STATE_PRESSED);
    }
    btn_player_play = buttons[2];
    btn_player_fav = buttons[4];

    lv_obj_t *vol = mk_box(scr_player, 320, 26);
    lv_obj_set_pos(vol, 0, 214);
    lbl_vol_low = mk_label(vol, LV_SYMBOL_MUTE, &lv_font_montserrat_14, C_INK2);
    lv_obj_set_pos(lbl_vol_low, 14, 5);
    lbl_vol_high = mk_label(vol, LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_14, C_INK2);
    lv_obj_set_pos(lbl_vol_high, 288, 5);
    sld_vol = lv_slider_create(vol);
    lv_obj_set_size(sld_vol, 246, 4);
    lv_obj_set_pos(sld_vol, 38, 11);
    lv_slider_set_range(sld_vol, 0, 100);
    lv_slider_set_value(sld_vol, player_volume(), LV_ANIM_OFF);
    lv_obj_set_style_radius(sld_vol, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sld_vol, C_RAISED, LV_PART_MAIN);
    lv_obj_set_style_border_width(sld_vol, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(sld_vol, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sld_vol, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sld_vol, 10, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sld_vol, C_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sld_vol, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(sld_vol, on_vol_change, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sld_vol, on_vol_release, LV_EVENT_RELEASED, NULL);

    build_drawer();
    build_donation_dialog();
}

static void build_list(void)
{
    scr_list = lv_obj_create(NULL);
    plain(scr_list);
    lv_obj_set_flex_flow(scr_list, LV_FLEX_FLOW_COLUMN);

    // 头部
    lv_obj_t *hdr = mk_box(scr_list, LV_PCT(100), HEADER_H);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, C_LINE, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);

    lv_obj_t *back = mk_btn(hdr, LV_SYMBOL_LEFT, &lv_font_montserrat_14, false, on_goto_player, NULL);
    lv_obj_set_size(back, 48, HEADER_H - 1);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);

    lbl_title = mk_label(hdr, "电台列表", &font_cjk_18, C_INK);
    lv_obj_center(lbl_title);

    btn_filter = mk_btn(hdr, "筛选", &font_cjk_14, false, on_filter_open, NULL);
    lv_obj_set_size(btn_filter, 56, HEADER_H - 1);
    lv_obj_set_style_border_width(btn_filter, 0, 0);
    lv_obj_align(btn_filter, LV_ALIGN_RIGHT_MID, 0, 0);
    lbl_filter = lv_obj_get_child(btn_filter, 0);

    // 主体：左侧竖排 tab + 右侧列表
    lv_obj_t *body = mk_box(scr_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);

    lv_obj_t *tabs = mk_box(body, 60, LV_PCT(100));
    lv_obj_set_style_border_side(tabs, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(tabs, C_LINE, 0);
    lv_obj_set_style_border_width(tabs, 1, 0);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_COLUMN);

    static const char *tab_names[3] = { "收藏", "精选", "台库" };
    // 顺序与 station_src_t 一致：FAV=0, PRESET=1, CATALOG=2
    for (int i = 0; i < 3; i++) {
        tab_btn[i] = mk_btn(tabs, tab_names[i], &font_cjk_14, false, on_tab_click, (void *)(intptr_t)i);
        lv_obj_set_width(tab_btn[i], 59);
        lv_obj_set_flex_grow(tab_btn[i], 1);
        lv_obj_set_style_border_width(tab_btn[i], 0, 0);
        lv_obj_set_style_border_side(tab_btn[i], LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(tab_btn[i], C_LINE, 0);
        lv_obj_set_style_border_width(tab_btn[i], 1, 0);
        tab_lbl[i] = lv_obj_get_child(tab_btn[i], 0);
    }

    list_content = mk_box(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(list_content, 1);
    lv_obj_set_flex_flow(list_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(list_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(list_content, 5, 0);
    lv_obj_set_style_pad_row(list_content, 4, 0);

    // 底部迷你播放条
    lv_obj_t *mini = mk_box(scr_list, LV_PCT(100), MINIBAR_H);
    lv_obj_set_style_border_side(mini, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(mini, C_INK, 0);
    lv_obj_set_style_border_width(mini, 1, 0);
    lv_obj_add_flag(mini, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mini, on_goto_player, LV_EVENT_CLICKED, NULL);

    lv_obj_set_style_bg_color(mini, C_SURFACE, 0);
    mini_icon = mk_label(mini, LV_SYMBOL_STOP, &lv_font_montserrat_14, C_ACCENT);
    lv_obj_align(mini_icon, LV_ALIGN_LEFT_MID, 8, 0);

    mini_name = mk_label(mini, "未选择电台", &font_cjk_14, C_INK);
    lv_obj_set_size(mini_name, 200, 18);
    lv_label_set_long_mode(mini_name, LV_LABEL_LONG_DOT);
    lv_obj_align(mini_name, LV_ALIGN_LEFT_MID, 30, 0);

    mini_state = mk_label(mini, "已停止", &font_cjk_14, C_INK2);
    lv_obj_align(mini_state, LV_ALIGN_RIGHT_MID, -8, 0);
}

static void build_chip_group(lv_obj_t *par, const char *title, int count,
                             const char *(*label_fn)(int), int (*count_fn)(int),
                             lv_event_cb_t cb, lv_obj_t ***store)
{
    lv_obj_t *lbl = mk_label(par, title, &font_cjk_14, C_INK2);
    lv_obj_set_style_pad_top(lbl, 4, 0);

    lv_obj_t *wrap = mk_box(par, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(wrap, 6, 0);
    lv_obj_set_style_pad_row(wrap, 6, 0);

    *store = lv_mem_alloc(sizeof(lv_obj_t *) * count);
    for (int i = 0; i < count; i++) {
        char text[32];
        int available = count_fn(i);
        snprintf(text, sizeof(text), "%s %d", label_fn(i), available);
        lv_obj_t *c = mk_btn(wrap, text, &font_cjk_14, false, cb, (void *)(intptr_t)i);
        lv_obj_set_size(c, LV_SIZE_CONTENT, 30);
        lv_obj_set_style_pad_left(c, 10, 0);
        lv_obj_set_style_pad_right(c, 10, 0);
        if (available == 0) lv_obj_add_state(c, LV_STATE_DISABLED);
        (*store)[i] = c;
    }
}

static void build_filter(void)
{
    scr_filter = lv_obj_create(NULL);
    plain(scr_filter);
    lv_obj_set_flex_flow(scr_filter, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *hdr = mk_box(scr_filter, LV_PCT(100), HEADER_H);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, C_LINE, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);

    lv_obj_t *close = mk_btn(hdr, LV_SYMBOL_CLOSE, &lv_font_montserrat_14, false, on_filter_cancel, NULL);
    lv_obj_set_size(close, 48, HEADER_H - 1);
    lv_obj_set_style_border_width(close, 0, 0);
    lv_obj_align(close, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *t = mk_label(hdr, "筛选", &font_cjk_18, C_INK);
    lv_obj_center(t);

    lv_obj_t *reset = mk_btn(hdr, "重置", &font_cjk_14, false, on_filter_reset, NULL);
    lv_obj_set_size(reset, 48, HEADER_H - 1);
    lv_obj_set_style_border_width(reset, 0, 0);
    lv_obj_align(reset, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *content = mk_box(scr_filter, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(content, 8, 0);
    lv_obj_set_style_pad_right(content, 8, 0);
    lv_obj_set_style_pad_row(content, 3, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);

    build_chip_group(content, "内容分类", stations_cat_count(), stations_cat_label,
                     stations_cat_station_count,
                     on_chip_cat, &chip_cat);
    mk_label(content, "地区", &font_cjk_14, C_INK2);
    char region_options[1024] = { 0 };
    size_t used = 0;
    for (int i = 0; i < stations_region_count(); ++i) {
        int n = snprintf(region_options + used, sizeof(region_options) - used, "%s %d%s",
                         stations_region_label(i), stations_region_station_count(i),
                         i + 1 < stations_region_count() ? "\n" : "");
        if (n < 0 || (size_t)n >= sizeof(region_options) - used) break;
        used += (size_t)n;
    }
    region_dropdown = lv_dropdown_create(content);
    lv_obj_set_size(region_dropdown, LV_PCT(100), 38);
    lv_dropdown_set_options(region_dropdown, region_options);
    // 默认下拉箭头是 Font Awesome 私有区字符，中文字体无法绘制。
    // 关闭内建符号并用矢量折线绘制，避免“全部 542”右侧出现豆腐块。
    lv_dropdown_set_symbol(region_dropdown, NULL);
    lv_obj_set_style_text_font(region_dropdown, &font_cjk_14, 0);
    lv_obj_set_style_text_color(region_dropdown, C_INK, 0);
    lv_obj_set_style_bg_color(region_dropdown, C_SURFACE, 0);
    lv_obj_set_style_border_color(region_dropdown, C_LINE, 0);
    lv_obj_set_style_radius(region_dropdown, 10, 0);
    lv_obj_set_style_pad_right(region_dropdown, 30, LV_PART_MAIN);
    lv_obj_add_event_cb(region_dropdown, on_region_change, LV_EVENT_VALUE_CHANGED, NULL);
    static const lv_point_t down_points[] = {{0, 1}, {5, 6}, {10, 1}};
    lv_obj_t *down = lv_line_create(region_dropdown);
    lv_line_set_points(down, down_points, 3);
    lv_obj_set_size(down, 11, 7);
    lv_obj_set_style_line_color(down, C_INK2, 0);
    lv_obj_set_style_line_width(down, 2, 0);
    lv_obj_set_style_line_rounded(down, true, 0);
    lv_obj_align(down, LV_ALIGN_RIGHT_MID, -10, 0);
    // 下拉列表是独立对象，必须显式设为深色，避免复现旧版黑白极性错乱。
    lv_dropdown_open(region_dropdown);
    lv_obj_t *region_list = lv_dropdown_get_list(region_dropdown);
    if (region_list) {
        lv_obj_set_style_bg_color(region_list, C_SURFACE, 0);
        lv_obj_set_style_text_color(region_list, C_INK, 0);
        lv_obj_set_style_text_font(region_list, &font_cjk_14, 0);
        lv_obj_set_style_border_color(region_list, C_LINE, 0);
        lv_obj_set_style_bg_color(region_list, C_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(region_list, C_ON_ACCENT,
                                    LV_PART_SELECTED | LV_STATE_CHECKED);
    }
    lv_dropdown_close(region_dropdown);

    lv_obj_t *bottom = mk_box(scr_filter, LV_PCT(100), 52);
    lv_obj_set_style_border_side(bottom, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(bottom, C_LINE, 0);
    lv_obj_set_style_border_width(bottom, 1, 0);
    lv_obj_set_style_pad_all(bottom, 6, 0);
    lv_obj_t *apply = mk_btn(bottom, "应用", &font_cjk_14, true, on_filter_apply, NULL);
    lv_obj_set_size(apply, LV_PCT(100), 40);
}

static int active_screen_id(void)
{
    lv_obj_t *active = lv_scr_act();
    if (active == scr_player) return UI_SCREEN_PLAYER;
    if (active == scr_list) return UI_SCREEN_STATIONS;
    if (active == scr_settings) return UI_SCREEN_SETTINGS;
    if (active == scr_filter) return UI_SCREEN_FILTER;
    if (active == scr_about) return UI_SCREEN_ABOUT;
    if (active == scr_ota) return UI_SCREEN_OTA;
    if (active == scr_diagnostics) return UI_SCREEN_DIAGNOSTICS;
    return -1;
}

static void clear_ui_object_refs(void)
{
    scr_player = scr_list = scr_filter = scr_setup = scr_settings = NULL;
    scr_wifi = scr_wifi_pass = NULL;
    scr_about = scr_ota = scr_diagnostics = scr_touch_test = NULL;
    lbl_net_state = wifi_content = wifi_ssid_input = wifi_password = wifi_keyboard = NULL;
    settings_body = settings_auto = settings_brightness = settings_accent_dot = NULL;
    settings_schedule_row = settings_dark_start = settings_dark_end = NULL;
    theme_picker_layer = theme_picker_wheel = theme_picker_preview = theme_picker_value = NULL;
    memset(settings_time_btn, 0, sizeof(settings_time_btn));
    memset(settings_theme_btn, 0, sizeof(settings_theme_btn));
    memset(theme_picker_preset_btn, 0, sizeof(theme_picker_preset_btn));
    lbl_name = lbl_meta = lbl_state = btn_player_play = btn_player_fav = NULL;
    home_update_banner = home_update_label = row_err = lbl_wifi = lbl_offline = NULL;
    lbl_vol_low = lbl_vol_high = sld_vol = NULL;
    lbl_clock = top_sleep_group = lbl_sleep_countdown = NULL;
    player_controls = home_visual = drawer_layer = drawer_sleep_label = NULL;
    donation_layer = screen_wake_layer = ota_body = diagnostics_body = NULL;
    touch_target = touch_dot = touch_status = touch_detail = touch_restart = NULL;
    visual_canvas = NULL;
    list_content = btn_filter = lbl_filter = lbl_title = NULL;
    mini_name = mini_state = mini_icon = NULL;
    memset(tab_btn, 0, sizeof(tab_btn));
    memset(tab_lbl, 0, sizeof(tab_lbl));
    region_dropdown = NULL;
}

static void rebuild_ui_for_theme(void *user_data)
{
    ui_screen_t target_id = (ui_screen_t)(intptr_t)user_data;
    lv_coord_t settings_scroll_y = settings_body ? lv_obj_get_scroll_y(settings_body) : 0;
    lv_obj_t *old_screens[] = {
        scr_player, scr_list, scr_filter, scr_setup, scr_settings,
        scr_wifi, scr_wifi_pass, scr_about, scr_ota, scr_diagnostics, scr_touch_test,
    };
    lv_obj_t *old_wake_layer = screen_wake_layer;
    lv_color_t *old_visual_buffer = visual_buffer;
    lv_obj_t **old_chip_cat = chip_cat;

    lv_obj_t *transition = lv_obj_create(NULL);
    plain(transition);
    lv_obj_t *transition_label = mk_label(transition, "正在应用外观…", &font_cjk_14, C_INK2);
    lv_obj_center(transition_label);
    lv_scr_load(transition);

    clear_ui_object_refs();
    visual_buffer = NULL;
    chip_cat = NULL;
    for (size_t i = 0; i < sizeof(old_screens) / sizeof(old_screens[0]); ++i) {
        if (old_screens[i]) lv_obj_del(old_screens[i]);
    }
    if (old_wake_layer) lv_obj_del(old_wake_layer);
    if (old_visual_buffer) heap_caps_free(old_visual_buffer);
    if (old_chip_cat) lv_mem_free(old_chip_cat);

    build_wifi();
    build_settings();
    build_touch_test();
    build_player();
    build_list();
    build_filter();
    build_screen_wake_layer();
    refresh_player();
    refresh_wifi();

    lv_obj_t *target = scr_player;
    switch (target_id) {
    case UI_SCREEN_STATIONS: refresh_list(); target = scr_list; break;
    case UI_SCREEN_SETTINGS: target = scr_settings; break;
    case UI_SCREEN_FILTER: s_draft = s_filter; refresh_chips(); target = scr_filter; break;
    case UI_SCREEN_ABOUT: build_about(); target = scr_about; break;
    case UI_SCREEN_OTA: build_ota_screen(); ota_render_session(); target = scr_ota; break;
    case UI_SCREEN_DIAGNOSTICS:
        build_diagnostics_screen(); refresh_diagnostics_screen(); target = scr_diagnostics; break;
    default: break;
    }
    lv_scr_load(target);
    if (target_id == UI_SCREEN_SETTINGS && settings_body) {
        lv_obj_update_layout(settings_body);
        lv_obj_scroll_to_y(settings_body, settings_scroll_y, LV_ANIM_OFF);
    }
    lv_obj_del(transition);
    s_theme_rebuild_scheduled = false;
    s_theme_rebuild_pending = false;
    ESP_LOGI(TAG, "外观已应用：mode=%d dark=%d accent=%d schedule=%02d-%02d",
             s_theme_mode, s_theme_dark, s_theme_accent,
             s_dark_start_hour, s_dark_end_hour);
}

static void request_theme_rebuild(void)
{
    if (s_theme_rebuild_scheduled) return;
    int active = active_screen_id();
    // 开机页到主页的 LVGL 屏幕动画尚未结束时，不能再发起一次
    // 屏幕切换；否则 LVGL 8 会在回收前一屏时留下悬空指针。
    if (!s_screen_awake || !s_boot_ready || active < 0) {
        s_theme_rebuild_pending = true;
        return;
    }
    s_theme_rebuild_scheduled = true;
    s_theme_rebuild_pending = false;
    lv_async_call(rebuild_ui_for_theme, (void *)(intptr_t)active);
}

// ------------------------------------------------------------------ 配网引导
static lv_obj_t *setup_screen(void)
{
    if (!scr_setup) {
        scr_setup = lv_obj_create(NULL);
        plain(scr_setup);
        lv_obj_set_flex_flow(scr_setup, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(scr_setup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(scr_setup, 14, 0);
        lv_obj_set_style_pad_row(scr_setup, 5, 0);
    }
    lv_obj_clean(scr_setup);
    return scr_setup;
}

static lv_obj_t *setup_line(lv_obj_t *par, const char *txt, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = mk_label(par, txt, f, c);
    lv_obj_set_width(l, 292);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

void ui_show_setup(const char *ap_ssid, const char *ip, const char *failed_ssid)
{
    if (!lvgl_port_lock(2000)) return;

    lv_obj_t *s = setup_screen();
    char buf[160];

    if (failed_ssid && failed_ssid[0]) {
        setup_line(s, "连不上原来的 Wi-Fi", &font_cjk_18, C_INK);
        snprintf(buf, sizeof(buf), "「%s」不可用，请重新配网", failed_ssid);
        setup_line(s, buf, &font_cjk_14, C_INK2);
    } else {
        setup_line(s, "联网设置", &font_cjk_18, C_INK);
    }

    lv_obj_t *local = mk_btn(s, "在本机选择 Wi-Fi", &font_cjk_18, true, on_wifi_open, NULL);
    lv_obj_set_size(local, 240, 44);
    setup_line(s, "仅支持 2.4GHz 网络", &font_cjk_14, C_INK2);

    snprintf(buf, sizeof(buf), "备用：手机连 %s，再打开 %s", ap_ssid, ip);
    setup_line(s, buf, &font_cjk_14, C_INK2);

    lv_scr_load(s);
    lvgl_port_unlock();
}

void ui_show_setup_connecting(const char *ssid)
{
    if (!lvgl_port_lock(2000)) return;

    lv_obj_t *s = setup_screen();
    setup_line(s, "正在连接…", &font_cjk_18, C_INK);
    if (ssid && ssid[0]) setup_line(s, ssid, &font_cjk_14, C_INK);
    setup_line(s, "连上会自动进播放页；若退回本页说明密码或网络有误，"
                  "重新连回热点再试一次", &font_cjk_14, C_INK2);

    lv_scr_load(s);
    lvgl_port_unlock();
}

void ui_hide_setup(void)
{
    if (!lvgl_port_lock(2000)) return;
    refresh_player();
    lv_scr_load(scr_player);
    lvgl_port_unlock();
}

// ------------------------------------------------------------------ 回调桥接
static void on_player_state(player_state_t st, const char *msg)
{
    (void)st; (void)msg;
    if (lvgl_port_lock(200)) {
        refresh_player();
        refresh_list();       // 行首的播放标识要跟着变
        lvgl_port_unlock();
    }
}

void ui_set_wifi_connected(bool connected)
{
    s_wifi_ok = connected;
    if (lvgl_port_lock(200)) {
        refresh_wifi();
        refresh_player();
        if (lbl_net_state) {
            char ssid[33];
            wifi_mgr_current_ssid(ssid, sizeof(ssid));
            lv_label_set_text(lbl_net_state, connected && ssid[0] ? ssid : "未连接");
        }
        lvgl_port_unlock();
    }
    if (connected && s_autoplay && !s_autoplay_done && s_has_station) {
        s_autoplay_done = true;
        player_play(&s_play_st);
    }
    if (connected && !s_ota_auto_checked) {
        s_ota_auto_checked = true;
        ota_mgr_check(on_ota_status, NULL);
    }
}

esp_err_t ui_show_screen(ui_screen_t screen)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    lv_obj_t *previous = lv_scr_act();
    bool same_info = (screen == UI_SCREEN_ABOUT && previous == scr_about) ||
                     (screen == UI_SCREEN_OTA && previous == scr_ota) ||
                     (screen == UI_SCREEN_DIAGNOSTICS && previous == scr_diagnostics);
    if (is_info_screen(previous) && !same_info) {
        lv_scr_load(scr_settings);
        release_info_screen(previous);
        previous = scr_settings;
    }
    lv_obj_t *target = NULL;
    if (screen != UI_SCREEN_WIFI && lv_scr_act() == scr_wifi) {
        s_wifi_visible = false;
        s_wifi_count = 0;
        lv_obj_clean(wifi_content);
    }
    switch (screen) {
    case UI_SCREEN_PLAYER:  refresh_player(); target = scr_player; break;
    case UI_SCREEN_STATIONS: refresh_list(); target = scr_list; break;
    case UI_SCREEN_SETTINGS: target = scr_settings; break;
    case UI_SCREEN_FILTER:   s_draft = s_filter; refresh_chips(); target = scr_filter; break;
    case UI_SCREEN_WIFI:     start_wifi_scan(); target = scr_wifi; break;
    case UI_SCREEN_WIFI_PASSWORD:
        lv_keyboard_set_textarea(wifi_keyboard, wifi_password);
        log_wifi_password_layout();
        target = scr_wifi_pass;
        break;
    case UI_SCREEN_ABOUT:
        if (!scr_about) build_about();
        target = scr_about;
        break;
    case UI_SCREEN_OTA:
        if (!scr_ota) build_ota_screen();
        ota_render_session();
        target = scr_ota;
        break;
    case UI_SCREEN_DIAGNOSTICS:
        if (!scr_diagnostics) build_diagnostics_screen();
        refresh_diagnostics_screen();
        target = scr_diagnostics;
        break;
    case UI_SCREEN_TOUCH_TEST:
        reset_touch_test();
        target = scr_touch_test;
        break;
    default: break;
    }
    if (target) {
        lv_scr_load(target);
    }
    lvgl_port_unlock();
    return target ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t ui_set_drawer_visible(bool visible)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    if (visible) {
        lv_obj_t *previous = lv_scr_act();
        refresh_player();
        lv_scr_load(scr_player);
        release_info_screen(previous);
        update_sleep_label();
        lv_obj_clear_flag(drawer_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(drawer_layer);
    } else {
        lv_obj_add_flag(drawer_layer, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_donation_visible(bool visible)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    if (visible) {
        lv_obj_t *previous = lv_scr_act();
        refresh_player();
        lv_scr_load(scr_player);
        release_info_screen(previous);
        lv_obj_add_flag(drawer_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(donation_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(donation_layer);
    } else {
        lv_obj_add_flag(donation_layer, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_display_awake(bool awake)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    set_display_awake_locked(awake);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_sleep_timer_seconds(int seconds)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    esp_err_t err = set_sleep_timer_seconds_locked(seconds);
    lvgl_port_unlock();
    return err;
}

esp_err_t ui_set_theme_mode(int mode)
{
    if (mode < THEME_MODE_LIGHT || mode > THEME_MODE_AUTO) return ESP_ERR_INVALID_ARG;
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    set_theme_mode_locked((theme_mode_t)mode);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_theme_accent(int accent)
{
    if (accent < 0 || accent >= 4) return ESP_ERR_INVALID_ARG;
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    if (accent != s_theme_accent) {
        s_theme_accent = accent;
        save_i32(NVS_KEY_THEME_ACCENT, accent);
        request_theme_rebuild();
    }
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_theme_custom_hue(int hue)
{
    if (hue < 0 || hue > 359) return ESP_ERR_INVALID_ARG;
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    bool changed = s_theme_accent != 4 || s_theme_custom_hue != hue;
    s_theme_accent = 4;
    s_theme_custom_hue = hue;
    save_i32(NVS_KEY_THEME_ACCENT, s_theme_accent);
    save_i32(NVS_KEY_CUSTOM_HUE, s_theme_custom_hue);
    if (changed) request_theme_rebuild();
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_dark_schedule(int start_hour, int end_hour)
{
    if (start_hour < 0 || start_hour >= 24 || end_hour < 0 || end_hour >= 24) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    set_dark_schedule_locked(start_hour, end_hour);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_show_appearance_settings(void)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    lv_scr_load(scr_settings);
    if (settings_schedule_row) lv_obj_scroll_to_view(settings_schedule_row, LV_ANIM_OFF);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_show_theme_picker(void)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    lv_scr_load(scr_settings);
    on_theme_picker_open(NULL);
    lvgl_port_unlock();
    return ESP_OK;
}

int ui_settings_scroll_y(void)
{
    if (!lvgl_port_lock(1000)) return -1;
    int y = settings_body ? (int)lv_obj_get_scroll_y(settings_body) : -1;
    lvgl_port_unlock();
    return y;
}

const char *ui_theme_mode_name(void)
{
    static const char *names[] = {"light", "dark", "auto"};
    return names[s_theme_mode];
}

bool ui_theme_is_dark(void) { return s_theme_dark; }
int ui_theme_accent(void) { return s_theme_accent; }
int ui_theme_custom_hue(void) { return s_theme_custom_hue; }
int ui_dark_start_hour(void) { return s_dark_start_hour; }
int ui_dark_end_hour(void) { return s_dark_end_hour; }

esp_err_t ui_play_catalog_index(int index)
{
    const station_t *station = stations_catalog_get(index);
    if (!station) return ESP_ERR_INVALID_ARG;
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    s_filter.region = STATION_REGION_ALL;
    s_filter.cat = STATION_CAT_ALL;
    s_play_st = *station;
    s_play_src = STATION_SRC_CATALOG;
    s_has_station = true;
    save_last_station();
    player_play(&s_play_st);
    refresh_player();
    lv_scr_load(scr_player);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_catalog_filter(int region, int category)
{
    if (region < 0 || region >= stations_region_count() ||
        category < 0 || category >= stations_cat_count()) return ESP_ERR_INVALID_ARG;
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    s_filter.region = region;
    s_filter.cat = category;
    s_tab = STATION_SRC_CATALOG;
    s_catalog_page = 0;
    save_tab();
    refresh_list();
    lv_scr_load(scr_list);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_step_station(int direction)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    step_station(direction < 0 ? -1 : 1);
    refresh_player();
    lv_scr_load(scr_player);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_init(void)
{
    // UI 比 Wi-Fi 管理器更早初始化。先设置北京时间，否则带 RTC 时间的热重启会按 UTC
    // 判断自动暗色，导致夜间启动画面短暂闪成亮色。
    setenv("TZ", "CST-8", 1);
    tzset();

    // 恢复用户偏好和上次电台；所有数据都有安全默认值。
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        int32_t t = STATION_SRC_PRESET;
        if (nvs_get_i32(h, NVS_KEY_TAB, &t) == ESP_OK && t >= 0 && t <= 2) {
            s_tab = (station_src_t)t;
        }
        int32_t value;
        if (nvs_get_i32(h, NVS_KEY_AUTO, &value) == ESP_OK) s_autoplay = value != 0;
        if (nvs_get_i32(h, NVS_KEY_BRIGHT, &value) == ESP_OK && value >= 10 && value <= 100) {
            s_brightness = value;
        }
        if (nvs_get_i32(h, NVS_KEY_TIMEFMT, &value) == ESP_OK) {
            s_time_24h = value != 12;
        }
        if (nvs_get_i32(h, NVS_KEY_THEME_MODE, &value) == ESP_OK &&
            value >= THEME_MODE_LIGHT && value <= THEME_MODE_AUTO) {
            s_theme_mode = (theme_mode_t)value;
        }
        if (nvs_get_i32(h, NVS_KEY_THEME_ACCENT, &value) == ESP_OK && value >= 0 && value <= 4) {
            s_theme_accent = value;
        }
        if (nvs_get_i32(h, NVS_KEY_CUSTOM_HUE, &value) == ESP_OK && value >= 0 && value <= 359) {
            s_theme_custom_hue = value;
        }
        if (nvs_get_i32(h, NVS_KEY_DARK_START, &value) == ESP_OK && value >= 0 && value < 24) {
            s_dark_start_hour = value;
        }
        if (nvs_get_i32(h, NVS_KEY_DARK_END, &value) == ESP_OK && value >= 0 && value < 24) {
            s_dark_end_hour = value;
        }
        if (nvs_get_i32(h, NVS_KEY_DARK_LAST, &value) == ESP_OK) {
            s_theme_dark = value != 0;
        }
        size_t last_len = sizeof(s_play_st);
        if (nvs_get_blob(h, NVS_KEY_LAST, &s_play_st, &last_len) == ESP_OK &&
            last_len == sizeof(s_play_st) && s_play_st.name[0] && s_play_st.url[0]) {
            s_play_st.name[sizeof(s_play_st.name) - 1] = '\0';
            s_play_st.url[sizeof(s_play_st.url) - 1] = '\0';
            s_play_st.meta[sizeof(s_play_st.meta) - 1] = '\0';
            s_has_station = true;
            int32_t src = STATION_SRC_PRESET;
            if (nvs_get_i32(h, NVS_KEY_LASTSRC, &src) == ESP_OK && src >= 0 && src <= 2) {
                s_play_src = (station_src_t)src;
            }
        }
        nvs_close(h);
    }

    // RTC/SNTP 已有有效时间时立即按时段判断；否则沿用上次自动模式结果。
    s_theme_dark = theme_desired_dark();

    if (!lvgl_port_lock(2000)) return ESP_FAIL;

    build_boot_screen();
    lv_scr_load(scr_boot);
    build_wifi();
    build_settings();
    build_touch_test();
    build_player();
    build_list();
    build_filter();
    build_screen_wake_layer();
    lv_timer_create(ui_live_timer, 40, NULL);
    lv_timer_t *boot_timer = lv_timer_create(boot_finish_cb, BOOT_HOLD_MS, NULL);
    lv_timer_set_repeat_count(boot_timer, 1);

    // 首次启动默认选中国内第一个台；是否自动播放由设置决定。
    if (!s_has_station && stations_preset_count() > 0) {
        s_play_st = *stations_preset_get(0);
        s_play_src = STATION_SRC_PRESET;
        s_has_station = true;
    }

    refresh_player();
    refresh_wifi();

    // board_display_init() 始终保持背光关闭。先同步刷出启动页第一帧，再点亮到
    // 用户保存的亮度，避免 LCD GRAM 尚未被首帧覆盖时出现短暂雪花。
    lv_refr_now(lv_disp_get_default());

    lvgl_port_unlock();

    // 首帧已完整写入 LCD；从全黑背光用硬件 PWM 渐亮，
    // 既不暴露未初始化的 GRAM，也不会阻塞 LVGL 后续动效。
    ESP_ERROR_CHECK(board_backlight_fade_to(s_brightness, 420));

    player_set_cb(on_player_state);
    ESP_LOGI(TAG, "界面就绪");
    return ESP_OK;
}

bool ui_responsive(void)
{
    if (!lvgl_port_lock(100)) return false;
    lvgl_port_unlock();
    return true;
}
