// 主题策略和色板；不持有页面指针，实际重建由 UI 协调层择机执行。
#include "ui_internal.h"

static lv_color_t custom_accent_color(int hue, bool dark);

static const uint32_t THEME_ACCENT_LIGHT[] = {0x635BFF, 0x007F73, 0xB85713, 0xC2386A};
static const uint32_t THEME_ACCENT_DARK[]  = {0x9A94FF, 0x3CCDB9, 0xFF9C5A, 0xFF7BA8};
static const uint32_t THEME_TINT_LIGHT[]   = {0xEEECFF, 0xE6F6F3, 0xFFF0E5, 0xFFEAF1};
static const uint32_t THEME_TINT_DARK[]    = {0x2B284F, 0x153B35, 0x452B1C, 0x472333};
static const char *THEME_ACCENT_NAMES[] = {"紫罗兰", "青绿色", "暖橙色", "玫红色", "自定义"};
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

lv_color_t ui_theme_accent_color(int accent, int hue, bool dark)
{
    if (accent >= 0 && accent < 4) {
        return lv_color_hex(dark ? THEME_ACCENT_DARK[accent] : THEME_ACCENT_LIGHT[accent]);
    }
    return custom_accent_color(hue, dark);
}

lv_color_t ui_theme_tint_color(int accent, int hue, bool dark)
{
    if (accent >= 0 && accent < 4) {
        return lv_color_hex(dark ? THEME_TINT_DARK[accent] : THEME_TINT_LIGHT[accent]);
    }
    return lv_color_hsv_to_rgb((uint16_t)hue, dark ? 45 : 18, dark ? 28 : 100);
}

const char *ui_theme_accent_name(int accent)
{
    if (accent < 0 || accent > 4) accent = 0;
    return THEME_ACCENT_NAMES[accent];
}


bool ui_theme_desired_dark(void)
{
    if (app_radio_state()->theme_mode == THEME_MODE_LIGHT) return false;
    if (app_radio_state()->theme_mode == THEME_MODE_DARK) return true;
    time_t now = time(NULL);
    struct tm local;
    if (now > 1700000000 && localtime_r(&now, &local)) {
        return app_radio_hour_is_dark(app_radio_state()->dark_start_hour, app_radio_state()->dark_end_hour, local.tm_hour);
    }
    // 首次联网校时前沿用上次自动判断，避免夜间重启先闪亮色。
    return app_radio_state()->theme_dark;
}

void ui_theme_apply_theme_choice(bool force_rebuild)
{
    bool dark = ui_theme_desired_dark();
    bool changed = dark != app_radio_state()->theme_dark;
    if (changed) {
        app_radio_state()->theme_dark = dark;
        preferences_save_i32(NVS_KEY_DARK_LAST, dark ? 1 : 0);
    }
    if (changed || force_rebuild) ui_request_theme_rebuild();
}

void ui_theme_set_theme_mode_locked(theme_mode_t mode)
{
    bool mode_changed = mode != app_radio_state()->theme_mode;
    app_radio_state()->theme_mode = mode;
    if (mode_changed) preferences_save_i32(NVS_KEY_THEME_MODE, mode);

    if (mode == THEME_MODE_AUTO) {
        // 用户主动选择“自动”时立即根据当前时间重算，
        // 并强制重绘一次，避免缓存状态与已绘制页面短暂不一致。
        ui_theme_apply_theme_choice(true);
        return;
    }

    bool dark = mode == THEME_MODE_DARK;
    if (dark != app_radio_state()->theme_dark) {
        app_radio_state()->theme_dark = dark;
        preferences_save_i32(NVS_KEY_DARK_LAST, dark ? 1 : 0);
    }
    if (mode_changed) ui_request_theme_rebuild();
}

void ui_theme_set_dark_schedule_locked(int start_hour, int end_hour)
{
    bool changed = start_hour != app_radio_state()->dark_start_hour || end_hour != app_radio_state()->dark_end_hour;
    app_radio_state()->dark_start_hour = start_hour;
    app_radio_state()->dark_end_hour = end_hour;
    if (changed) {
        preferences_save_i32(NVS_KEY_DARK_START, start_hour);
        preferences_save_i32(NVS_KEY_DARK_END, end_hour);
    }
    // 修改时段属于用户主动操作；自动模式下必须当场重算与刷新。
    if (app_radio_state()->theme_mode == THEME_MODE_AUTO) ui_theme_apply_theme_choice(true);
}

esp_err_t ui_set_theme_mode(int mode)
{
    if (mode < THEME_MODE_LIGHT || mode > THEME_MODE_AUTO) return ESP_ERR_INVALID_ARG;
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    ui_theme_set_theme_mode_locked((theme_mode_t)mode);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_theme_accent(int accent)
{
    if (accent < 0 || accent >= 4) return ESP_ERR_INVALID_ARG;
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    if (accent != app_radio_state()->theme_accent) {
        app_radio_state()->theme_accent = accent;
        preferences_save_i32(NVS_KEY_THEME_ACCENT, accent);
        ui_request_theme_rebuild();
    }
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_theme_custom_hue(int hue)
{
    if (hue < 0 || hue > 359) return ESP_ERR_INVALID_ARG;
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    bool changed = app_radio_state()->theme_accent != 4 || app_radio_state()->theme_custom_hue != hue;
    app_radio_state()->theme_accent = 4;
    app_radio_state()->theme_custom_hue = hue;
    preferences_save_i32(NVS_KEY_THEME_ACCENT, app_radio_state()->theme_accent);
    preferences_save_i32(NVS_KEY_CUSTOM_HUE, app_radio_state()->theme_custom_hue);
    if (changed) ui_request_theme_rebuild();
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_dark_schedule(int start_hour, int end_hour)
{
    if (start_hour < 0 || start_hour >= 24 || end_hour < 0 || end_hour >= 24) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    ui_theme_set_dark_schedule_locked(start_hour, end_hour);
    lvgl_port_unlock();
    return ESP_OK;
}

// 串口诊断运行在另一个任务。即使这里只读一个标量，也遵守应用状态的锁约定。
const char *ui_theme_mode_name(void)
{
    static const char *names[] = {"light", "dark", "auto"};
    if (!lvgl_port_lock(1000)) return "unknown";
    const char *name = names[app_radio_state()->theme_mode];
    lvgl_port_unlock();
    return name;
}

bool ui_theme_is_dark(void)
{
    if (!lvgl_port_lock(1000)) return false;
    bool value = app_radio_state()->theme_dark;
    lvgl_port_unlock();
    return value;
}

int ui_theme_accent(void)
{
    if (!lvgl_port_lock(1000)) return -1;
    int value = app_radio_state()->theme_accent;
    lvgl_port_unlock();
    return value;
}

int ui_theme_custom_hue(void)
{
    if (!lvgl_port_lock(1000)) return -1;
    int value = app_radio_state()->theme_custom_hue;
    lvgl_port_unlock();
    return value;
}

int ui_dark_start_hour(void)
{
    if (!lvgl_port_lock(1000)) return -1;
    int value = app_radio_state()->dark_start_hour;
    lvgl_port_unlock();
    return value;
}

int ui_dark_end_hour(void)
{
    if (!lvgl_port_lock(1000)) return -1;
    int value = app_radio_state()->dark_end_hour;
    lvgl_port_unlock();
    return value;
}
