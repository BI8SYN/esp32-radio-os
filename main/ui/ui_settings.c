// 设置页持有控件和选色草稿；应用/持久化由显式操作触发，预览不改正式偏好。
#include "ui_internal.h"

static void on_auto_change(lv_event_t *e);
static void on_brightness_change(lv_event_t *e);
static void on_brightness_release(lv_event_t *e);
static void on_close_settings(lv_event_t *e);
static void refresh_time_format_buttons(void);
static void on_time_format(lv_event_t *e);
static void refresh_theme_mode_buttons(void);
static void on_theme_mode(lv_event_t *e);
static void on_theme_schedule(lv_event_t *e);
static void draw_schedule_connector(lv_event_t *e);
static void style_hour_dropdown(lv_obj_t *dropdown);
static void refresh_theme_picker(void);
static void on_theme_picker_wheel(lv_event_t *e);
static void on_theme_picker_preset(lv_event_t *e);
static void on_theme_picker_close(lv_event_t *e);
static void on_theme_picker_backdrop(lv_event_t *e);
static void on_theme_picker_apply(lv_event_t *e);
static void build_theme_picker(void);
static void on_theme_picker_open(lv_event_t *e);

static const char THEME_HOUR_OPTIONS[] =
    "00:00\n01:00\n02:00\n03:00\n04:00\n05:00\n06:00\n07:00\n"
    "08:00\n09:00\n10:00\n11:00\n12:00\n13:00\n14:00\n15:00\n"
    "16:00\n17:00\n18:00\n19:00\n20:00\n21:00\n22:00\n23:00";

static lv_obj_t *scr_settings, *lbl_net_state;
static lv_obj_t *settings_body, *settings_auto, *settings_brightness, *settings_time_btn[2];
static lv_obj_t *settings_theme_btn[3], *settings_accent_dot;
static lv_obj_t *settings_schedule_row, *settings_dark_start, *settings_dark_end;
static lv_obj_t *theme_picker_layer, *theme_picker_wheel, *theme_picker_preview;
static lv_obj_t *theme_picker_value, *theme_picker_preset_btn[4];
static int s_picker_pending_accent, s_picker_pending_hue;
static bool s_picker_syncing;
static void on_auto_change(lv_event_t *e)
{
    app_radio_state()->autoplay = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    preferences_save_i32(NVS_KEY_AUTO, app_radio_state()->autoplay ? 1 : 0);
}

static void on_brightness_change(lv_event_t *e)
{
    app_radio_state()->brightness = lv_slider_get_value(lv_event_get_target(e));
    board_backlight_level(app_radio_state()->brightness);
}

static void on_brightness_release(lv_event_t *e)
{
    (void)e;
    preferences_save_i32(NVS_KEY_BRIGHT, app_radio_state()->brightness);
}

static void on_close_settings(lv_event_t *e)
{
    (void)e;
    ui_player_refresh();
    ui_navigate_locked(UI_SCREEN_PLAYER);
}

static void refresh_time_format_buttons(void)
{
    for (int i = 0; i < 2; i++) {
        if (!settings_time_btn[i]) continue;
        bool selected = (i == 0) == app_radio_state()->time_24h;
        lv_obj_set_style_bg_color(settings_time_btn[i], selected ? C_ACCENT : C_SURFACE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(settings_time_btn[i], 0),
                                    selected ? C_ON_ACCENT : C_INK2, 0);
    }
}

static void on_time_format(lv_event_t *e)
{
    int fmt = (int)(intptr_t)lv_event_get_user_data(e);
    app_radio_state()->time_24h = fmt == 0;
    preferences_save_i32(NVS_KEY_TIMEFMT, app_radio_state()->time_24h ? 24 : 12);
    refresh_time_format_buttons();
}

static void refresh_theme_mode_buttons(void)
{
    for (int i = 0; i < 3; ++i) {
        if (!settings_theme_btn[i]) continue;
        bool selected = i == (int)app_radio_state()->theme_mode;
        lv_obj_set_style_bg_color(settings_theme_btn[i], selected ? C_ACCENT : C_SURFACE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(settings_theme_btn[i], 0),
                                    selected ? C_ON_ACCENT : C_INK2, 0);
    }
    if (settings_schedule_row) {
        if (app_radio_state()->theme_mode == THEME_MODE_AUTO)
            lv_obj_clear_flag(settings_schedule_row, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(settings_schedule_row, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_theme_mode(lv_event_t *e)
{
    int mode = (int)(intptr_t)lv_event_get_user_data(e);
    if (mode < THEME_MODE_LIGHT || mode > THEME_MODE_AUTO) return;
    if (mode == app_radio_state()->theme_mode && mode != THEME_MODE_AUTO) return;
    ui_theme_set_theme_mode_locked((theme_mode_t)mode);
}

static void on_theme_schedule(lv_event_t *e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    int hour = (int)lv_dropdown_get_selected(dropdown);
    if (dropdown == settings_dark_start) {
        ui_theme_set_dark_schedule_locked(hour, app_radio_state()->dark_end_hour);
    } else {
        ui_theme_set_dark_schedule_locked(app_radio_state()->dark_start_hour, hour);
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
    lv_color_t selected = ui_theme_accent_color(s_picker_pending_accent,
                                                  s_picker_pending_hue, app_radio_state()->theme_dark);
    lv_obj_set_style_bg_color(theme_picker_preview, selected, 0);
    if (theme_picker_value) {
        char value[32];
        if (s_picker_pending_accent == 4) {
            snprintf(value, sizeof(value), "自定义 %d°", s_picker_pending_hue);
        } else {
            snprintf(value, sizeof(value), "%s", ui_theme_accent_name(s_picker_pending_accent));
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
    lv_color_hsv_t hsv = lv_color_to_hsv(ui_theme_accent_color(accent, 260, false));
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
    bool changed = app_radio_state()->theme_accent != s_picker_pending_accent ||
                   (s_picker_pending_accent == 4 && app_radio_state()->theme_custom_hue != s_picker_pending_hue);
    app_radio_state()->theme_accent = s_picker_pending_accent;
    app_radio_state()->theme_custom_hue = s_picker_pending_hue;
    preferences_save_i32(NVS_KEY_THEME_ACCENT, app_radio_state()->theme_accent);
    preferences_save_i32(NVS_KEY_CUSTOM_HUE, app_radio_state()->theme_custom_hue);
    if (changed) {
        if (theme_picker_layer) lv_obj_add_flag(theme_picker_layer, LV_OBJ_FLAG_HIDDEN);
        ui_request_theme_rebuild();
    } else {
        on_theme_picker_close(NULL);
    }
}

static void build_theme_picker(void)
{
    theme_picker_layer = ui_widgets_mk_box(scr_settings, 320, 240);
    lv_obj_add_flag(theme_picker_layer, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(theme_picker_layer, 0, 0);
    lv_obj_set_style_bg_color(theme_picker_layer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(theme_picker_layer, LV_OPA_50, 0);
    lv_obj_add_event_cb(theme_picker_layer, on_theme_picker_backdrop, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = ui_widgets_mk_box(theme_picker_layer, 296, 224);
    lv_obj_center(card);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(card, C_PAPER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, C_LINE, 0);

    lv_obj_t *title = ui_widgets_mk_label(card, "选择主题色", &font_cjk_18, C_INK);
    lv_obj_set_pos(title, 16, 12);
    lv_obj_t *close = ui_widgets_mk_btn(card, LV_SYMBOL_CLOSE, &lv_font_montserrat_14,
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

    theme_picker_preview = ui_widgets_mk_box(card, 52, 52);
    lv_obj_set_pos(theme_picker_preview, 190, 50);
    lv_obj_set_style_radius(theme_picker_preview, LV_RADIUS_CIRCLE, 0);

    theme_picker_value = ui_widgets_mk_label(card, "", &font_cjk_14, C_INK2);
    lv_obj_set_width(theme_picker_value, 124);
    lv_obj_set_style_text_align(theme_picker_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(theme_picker_value, 154, 106);

    lv_obj_t *apply = ui_widgets_mk_btn(card, "应用", &font_cjk_14, true,
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
        lv_obj_t *dot = ui_widgets_mk_box(button, 24, 24);
        // 色点是按钮的装饰，不能截走中心位置的点击；整块按钮都应可选色。
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, ui_theme_accent_color(i, 260, false), 0);
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
    s_picker_pending_accent = app_radio_state()->theme_accent;
    s_picker_pending_hue = app_radio_state()->theme_custom_hue;
    int wheel_hue = s_picker_pending_hue;
    if (s_picker_pending_accent >= 0 && s_picker_pending_accent < 4) {
        wheel_hue = lv_color_to_hsv(ui_theme_accent_color(s_picker_pending_accent, 260, false)).h;
    }
    s_picker_syncing = true;
    lv_colorwheel_set_hsv(theme_picker_wheel, (lv_color_hsv_t){wheel_hue, 72, 82});
    s_picker_syncing = false;
    refresh_theme_picker();
    lv_obj_move_foreground(theme_picker_layer);
    lv_obj_clear_flag(theme_picker_layer, LV_OBJ_FLAG_HIDDEN);
}

void ui_settings_build(void)
{
    scr_settings = lv_obj_create(NULL);
    ui_widgets_plain(scr_settings);
    lv_obj_set_flex_flow(scr_settings, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *hdr = ui_widgets_mk_box(scr_settings, LV_PCT(100), HEADER_H);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, C_LINE, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);

    lv_obj_t *back = ui_widgets_mk_btn(hdr, LV_SYMBOL_LEFT, &lv_font_montserrat_14, false, on_close_settings, NULL);
    lv_obj_set_size(back, 48, HEADER_H - 1);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *t = ui_widgets_mk_label(hdr, "设置", &font_cjk_18, C_INK);
    lv_obj_center(t);

    settings_body = ui_widgets_mk_box(scr_settings, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_t *body = settings_body;
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    ui_widgets_setting_nav_row(body, "Wi-Fi", "未连接", ui_wifi_on_wifi_open, &lbl_net_state);

    lv_obj_t *auto_row = ui_widgets_mk_box(body, LV_PCT(100), ROW_H);
    lv_obj_set_style_border_side(auto_row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(auto_row, C_LINE, 0);
    lv_obj_set_style_border_width(auto_row, 1, 0);
    lv_obj_t *auto_label = ui_widgets_mk_label(auto_row, "联网后自动续播", &font_cjk_14, C_INK);
    lv_obj_align(auto_label, LV_ALIGN_LEFT_MID, 8, 0);
    settings_auto = lv_switch_create(auto_row);
    lv_obj_set_size(settings_auto, 46, 28); lv_obj_align(settings_auto, LV_ALIGN_RIGHT_MID, -8, 0);
    if (app_radio_state()->autoplay) lv_obj_add_state(settings_auto, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(settings_auto, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(settings_auto, on_auto_change, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *time_row = ui_widgets_mk_box(body, LV_PCT(100), 56);
    lv_obj_set_style_border_side(time_row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(time_row, C_LINE, 0);
    lv_obj_set_style_border_width(time_row, 1, 0);
    lv_obj_t *time_label = ui_widgets_mk_label(time_row, "时间格式", &font_cjk_14, C_INK);
    lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_t *time_group = ui_widgets_mk_box(time_row, 120, 44);
    lv_obj_set_style_bg_color(time_group, C_SURFACE, 0);
    lv_obj_set_style_radius(time_group, 10, 0);
    lv_obj_align(time_group, LV_ALIGN_RIGHT_MID, -8, 0);
    for (int i = 0; i < 2; i++) {
        settings_time_btn[i] = ui_widgets_mk_btn(time_group, i == 0 ? "24 小时" : "12 小时",
                                      &font_cjk_14, false, on_time_format,
                                      (void *)(intptr_t)i);
        lv_obj_set_size(settings_time_btn[i], 60, 44);
        lv_obj_align(settings_time_btn[i], i == 0 ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, 0, 0);
    }
    refresh_time_format_buttons();

    lv_obj_t *theme_mode_row = ui_widgets_mk_box(body, LV_PCT(100), 56);
    lv_obj_set_style_border_side(theme_mode_row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(theme_mode_row, C_LINE, 0);
    lv_obj_set_style_border_width(theme_mode_row, 1, 0);
    lv_obj_t *theme_mode_label = ui_widgets_mk_label(theme_mode_row, "外观模式", &font_cjk_14, C_INK);
    lv_obj_align(theme_mode_label, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_t *theme_mode_group = ui_widgets_mk_box(theme_mode_row, 174, 44);
    lv_obj_set_style_bg_color(theme_mode_group, C_SURFACE, 0);
    lv_obj_set_style_radius(theme_mode_group, 10, 0);
    lv_obj_align(theme_mode_group, LV_ALIGN_RIGHT_MID, -8, 0);
    static const char *mode_labels[] = {"亮色", "暗色", "自动"};
    for (int i = 0; i < 3; ++i) {
        settings_theme_btn[i] = ui_widgets_mk_btn(theme_mode_group, mode_labels[i], &font_cjk_14,
                                       false, on_theme_mode, (void *)(intptr_t)i);
        lv_obj_set_size(settings_theme_btn[i], 58, 44);
        lv_obj_set_pos(settings_theme_btn[i], i * 58, 0);
    }

    settings_schedule_row = ui_widgets_mk_box(body, LV_PCT(100), 52);
    lv_obj_set_style_border_side(settings_schedule_row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(settings_schedule_row, C_LINE, 0);
    lv_obj_set_style_border_width(settings_schedule_row, 1, 0);
    lv_obj_t *schedule_label = ui_widgets_mk_label(settings_schedule_row, "暗色时段", &font_cjk_14, C_INK);
    lv_obj_align(schedule_label, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_t *schedule_group = ui_widgets_mk_box(settings_schedule_row, 172, 40);
    lv_obj_set_flex_flow(schedule_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(schedule_group, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(schedule_group, LV_ALIGN_RIGHT_MID, -8, 0);
    settings_dark_start = lv_dropdown_create(schedule_group);
    lv_obj_set_size(settings_dark_start, 76, 40);
    style_hour_dropdown(settings_dark_start);
    lv_dropdown_set_selected(settings_dark_start, app_radio_state()->dark_start_hour);
    // 在透明区域里直接绘制，不依赖字体或超扁对象的 flex 尺寸解释。
    lv_obj_t *schedule_to = ui_widgets_mk_box(schedule_group, 12, 16);
    lv_obj_set_style_bg_opa(schedule_to, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(schedule_to, draw_schedule_connector, LV_EVENT_DRAW_MAIN_END, NULL);
    settings_dark_end = lv_dropdown_create(schedule_group);
    lv_obj_set_size(settings_dark_end, 76, 40);
    style_hour_dropdown(settings_dark_end);
    lv_dropdown_set_selected(settings_dark_end, app_radio_state()->dark_end_hour);
    (void)schedule_to;

    lv_obj_t *accent_row = ui_widgets_setting_nav_row(body, "主题色",
                                            ui_theme_accent_name(app_radio_state()->theme_accent),
                                            on_theme_picker_open, NULL);
    lv_obj_set_height(accent_row, 52);
    settings_accent_dot = ui_widgets_mk_box(accent_row, 18, 18);
    lv_obj_set_style_radius(settings_accent_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(settings_accent_dot, C_ACCENT, 0);
    lv_obj_align(settings_accent_dot, LV_ALIGN_RIGHT_MID, -88, 0);
    refresh_theme_mode_buttons();

    lv_obj_t *bright = ui_widgets_mk_box(body, LV_PCT(100), ROW_H);
    lv_obj_set_style_border_side(bright, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bright, C_LINE, 0);
    lv_obj_set_style_border_width(bright, 1, 0);
    lv_obj_t *bl = ui_widgets_mk_label(bright, "屏幕亮度", &font_cjk_14, C_INK); lv_obj_align(bl, LV_ALIGN_LEFT_MID, 8, 0);
    settings_brightness = lv_slider_create(bright);
    lv_obj_set_size(settings_brightness, 145, 4); lv_obj_align(settings_brightness, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_slider_set_range(settings_brightness, 10, 100);
    lv_slider_set_value(settings_brightness, app_radio_state()->brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(settings_brightness, C_RAISED, LV_PART_MAIN);
    lv_obj_set_style_bg_color(settings_brightness, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(settings_brightness, C_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(settings_brightness, 7, LV_PART_KNOB);
    lv_obj_add_event_cb(settings_brightness, on_brightness_change, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(settings_brightness, on_brightness_release, LV_EVENT_RELEASED, NULL);

    ui_widgets_setting_nav_row(body, "系统更新", ota_mgr_running_version(), ui_info_on_ota_open, NULL);
    ui_widgets_setting_nav_row(body, "诊断", NULL, ui_info_on_diagnostics_open, NULL);
    ui_widgets_setting_nav_row(body, "关于", NULL, ui_info_on_about_open, NULL);
}

void ui_settings_on_open_settings(lv_event_t *e)
{
    (void)e;
    ui_settings_network_changed();
    ui_navigate_locked(UI_SCREEN_SETTINGS);
}

esp_err_t ui_show_appearance_settings(void)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    ui_navigate_locked(UI_SCREEN_SETTINGS);
    if (settings_schedule_row) lv_obj_scroll_to_view(settings_schedule_row, LV_ANIM_OFF);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_show_theme_picker(void)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    ui_navigate_locked(UI_SCREEN_SETTINGS);
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

lv_obj_t *ui_settings_screen(ui_screen_t id)
{
    switch (id) {
    case UI_SCREEN_SETTINGS: return scr_settings;
    default: return NULL;
    }
}

// 仅在已切到过渡屏幕后调用：父屏幕先销毁子控件，再清空本模块句柄。
void ui_settings_destroy(void)
{
    if (scr_settings) lv_obj_del(scr_settings);
    scr_settings = NULL;
    lbl_net_state = NULL;
    settings_body = NULL;
    settings_auto = NULL;
    settings_brightness = NULL;
    memset(settings_time_btn, 0, sizeof(settings_time_btn));
    memset(settings_theme_btn, 0, sizeof(settings_theme_btn));
    settings_accent_dot = NULL;
    settings_schedule_row = NULL;
    settings_dark_start = NULL;
    settings_dark_end = NULL;
    theme_picker_layer = NULL;
    theme_picker_wheel = NULL;
    theme_picker_preview = NULL;
    theme_picker_value = NULL;
    memset(theme_picker_preset_btn, 0, sizeof(theme_picker_preset_btn));
}

void ui_settings_network_changed(void)
{
    if (!lbl_net_state) return;
    char ssid[33];
    wifi_mgr_current_ssid(ssid, sizeof(ssid));
    lv_label_set_text(lbl_net_state, app_radio_state()->wifi_connected && ssid[0] ? ssid : "未连接");
}
int ui_settings_scroll_locked(void) { return settings_body ? lv_obj_get_scroll_y(settings_body) : 0; }
void ui_settings_restore_scroll(int y)
{
    if (!settings_body) return;
    lv_obj_update_layout(settings_body);
    lv_obj_scroll_to_y(settings_body, y, LV_ANIM_OFF);
}
