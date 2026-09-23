// 播放页与其抽屉/遮罩的控件只在本文件持有；跨页面状态从 app_radio 获取。
#include "ui_internal.h"
#include "assets/donation_qr.h"

typedef enum { PLAYER_ICON_LIST, PLAYER_ICON_PREV, PLAYER_ICON_PLAY, PLAYER_ICON_NEXT } player_icon_t;
static lv_coord_t morph_coord(lv_coord_t from, lv_coord_t to);
static void draw_player_icon(lv_event_t *e);
static lv_obj_t *mk_player_icon_btn(lv_obj_t *parent, player_icon_t kind,
                                    lv_event_cb_t callback);
static void set_play_morph(void *object, int32_t value);
static void update_play_icon(bool active);
static void on_toggle_play(lv_event_t *e);
static void on_player_fav(lv_event_t *e);
static void on_prev(lv_event_t *e);
static void on_next(lv_event_t *e);
static void on_retry_play(lv_event_t *e);
static void on_goto_list(lv_event_t *e);
static void on_vol_change(lv_event_t *e);
static void on_vol_release(lv_event_t *e);
static void on_screen_wake(lv_event_t *e);
static void on_screen_off(lv_event_t *e);
static void on_sleep_cycle(lv_event_t *e);
static void on_drawer_close(lv_event_t *e);
static void on_drawer_open(lv_event_t *e);
static void on_drawer_settings(lv_event_t *e);
static void on_donation_close(lv_event_t *e);
static void on_donation_open(lv_event_t *e);
static lv_obj_t *build_coffee_icon(lv_obj_t *parent);
static void build_donation_dialog(void);
static void build_drawer(void);

static lv_obj_t *scr_player;
static lv_obj_t *lbl_name, *lbl_meta, *lbl_state, *btn_player_play, *btn_player_fav;
static lv_obj_t *home_update_banner, *home_update_label;
static lv_obj_t *row_err, *lbl_wifi, *lbl_offline, *lbl_vol_low, *lbl_vol_high, *sld_vol;
static lv_obj_t *btn_shuffle;
static lv_obj_t *lbl_clock, *top_sleep_group, *lbl_sleep_countdown;
static lv_obj_t *player_controls, *home_visual, *drawer_layer, *drawer_sleep_label;
static lv_obj_t *donation_layer, *screen_wake_layer;
static int32_t s_play_morph;
static bool s_play_icon_active;

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

void ui_player_refresh_wifi(void)
{
    if (!lbl_wifi) return;
    lv_obj_set_style_text_color(lbl_wifi, app_radio_state()->wifi_connected ? C_ACCENT : C_INK2, 0);
    if (lbl_offline) {
        if (app_radio_state()->wifi_connected) lv_obj_add_flag(lbl_offline, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(lbl_offline, LV_OBJ_FLAG_HIDDEN);
    }
    if (home_visual) {
        lv_obj_set_style_opa(home_visual, app_radio_state()->wifi_connected ? LV_OPA_COVER : LV_OPA_0, 0);
        lv_obj_clear_flag(home_visual, LV_OBJ_FLAG_CLICKABLE);
    }
}

void ui_player_refresh_ota_banner(void)
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

void ui_player_refresh(void)
{
    if (!lbl_name) return;

    if (app_radio_state()->has_station) {
        lv_label_set_text(lbl_name, app_radio_state()->play_station.name);
        lv_label_set_text(lbl_meta, app_radio_state()->play_station.meta);
    } else {
        lv_label_set_text(lbl_name, "未选择电台");
        lv_label_set_text(lbl_meta, "");
    }

    // 状态文案全部走 CJK 字库，不混 Montserrat 符号（两个字库合不到一个 label 里）
    player_state_t ps = player_state();
    const char *txt;
    if (!app_radio_state()->wifi_connected) txt = "网络未连接 · 点右上角连接网络";
    else switch (ps) {
        case PLAYER_PLAYING:   txt = "正在播放"; break;
        case PLAYER_BUFFERING: txt = "正在连接…"; break;
        case PLAYER_ERROR:     txt = player_error_msg(); break;
        default:               txt = "已暂停"; break;
    }
    lv_label_set_text(lbl_state, txt);
    lv_obj_set_style_text_color(lbl_state, app_radio_state()->wifi_connected && ps == PLAYER_PLAYING ? C_ACCENT : C_INK2, 0);
    ui_player_refresh_ota_banner();

    if (app_radio_state()->wifi_connected && ps == PLAYER_ERROR) {
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
        bool fav = app_radio_state()->has_station && stations_fav_contains(app_radio_state()->play_station.url);
        if (fav) lv_obj_add_state(btn_player_fav, LV_STATE_CHECKED);
        else lv_obj_clear_state(btn_player_fav, LV_STATE_CHECKED);
        lv_obj_invalidate(btn_player_fav);
    }

    ui_stations_refresh_minibar();
}

static void on_toggle_play(lv_event_t *e)
{
    (void)e;
    player_state_t ps = player_state();
    if (ps == PLAYER_PLAYING || ps == PLAYER_BUFFERING) {
        player_stop();
    } else if (app_radio_state()->has_station) {
        player_play(&app_radio_state()->play_station);
    }
    ui_player_refresh();
}

static void on_player_fav(lv_event_t *e)
{
    (void)e;
    if (!app_radio_state()->has_station) return;
    if (stations_fav_contains(app_radio_state()->play_station.url)) stations_fav_remove(app_radio_state()->play_station.url);
    else stations_fav_add(&app_radio_state()->play_station);
    ui_player_refresh();
    ui_stations_refresh_list();
}

// 与 Wi-Fi 一样独立的 44px 点击区，图标本身不拦截触摸。
static void on_shuffle(lv_event_t *e)
{
    (void)e;
    app_radio_set_shuffle(!app_radio_state()->shuffle);
    if (app_radio_state()->shuffle) lv_obj_add_state(btn_shuffle, LV_STATE_CHECKED);
    else lv_obj_clear_state(btn_shuffle, LV_STATE_CHECKED);
}

static void on_prev(lv_event_t *e) { (void)e; app_radio_step_station(-1); ui_player_refresh(); }

static void on_next(lv_event_t *e) { (void)e; app_radio_step_station(1); ui_player_refresh(); }

static void on_retry_play(lv_event_t *e)
{
    (void)e;
    if (app_radio_state()->has_station) player_play(&app_radio_state()->play_station);
    ui_player_refresh();
}

static void on_goto_list(lv_event_t *e)
{
    (void)e;
    ui_stations_refresh_list();
    ui_navigate_locked(UI_SCREEN_STATIONS);
}

void ui_player_on_goto_player(lv_event_t *e)
{
    (void)e;
    ui_player_refresh();
    ui_navigate_locked(UI_SCREEN_PLAYER);
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

void ui_player_set_display_awake_locked(bool awake)
{
    if (awake == app_radio_state()->screen_awake) return;
    app_radio_state()->screen_awake = awake;
    if (awake) {
        board_display_power(true);
        if (screen_wake_layer) lv_obj_add_flag(screen_wake_layer, LV_OBJ_FLAG_HIDDEN);
        ui_apply_pending_theme();
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
    ui_player_set_display_awake_locked(true);
}

static void on_screen_off(lv_event_t *e)
{
    (void)e;
    ui_player_set_display_awake_locked(false);
}

void ui_player_update_sleep_label(void)
{
    if (!drawer_sleep_label) return;
    char text[16];
    if (app_radio_state()->sleep_minutes == 0) snprintf(text, sizeof(text), "睡眠关闭");
    else snprintf(text, sizeof(text), "睡眠%d分", app_radio_state()->sleep_minutes);
    lv_label_set_text(drawer_sleep_label, text);
}

void ui_player_update_sleep_countdown(void)
{
    if (!top_sleep_group || !lbl_sleep_countdown) return;
    int64_t deadline = app_radio_state()->sleep_deadline_us;
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

static void on_sleep_cycle(lv_event_t *e)
{
    (void)e;
    static const int choices[] = { 0, 15, 30, 60, 90 };
    int pos = 0;
    for (int i = 0; i < 5; i++) if (choices[i] == app_radio_state()->sleep_minutes) pos = i;
    app_radio_state()->sleep_minutes = choices[(pos + 1) % 5];
    app_radio_set_sleep(app_radio_state()->sleep_minutes * 60);
    ui_player_update_sleep_label();
    ui_player_update_sleep_countdown();
}

static void on_drawer_close(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(drawer_layer, LV_OBJ_FLAG_HIDDEN);
}

static void on_drawer_open(lv_event_t *e)
{
    (void)e;
    ui_player_update_sleep_label();
    lv_obj_clear_flag(drawer_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(drawer_layer);
}

static void on_drawer_settings(lv_event_t *e)
{
    on_drawer_close(e);
    ui_settings_on_open_settings(e);
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
    lv_obj_t *icon = ui_widgets_mk_box(parent, 20, 20);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_t *cup = ui_widgets_mk_box(icon, 13, 10);
    lv_obj_set_pos(cup, 1, 4);
    lv_obj_set_style_bg_opa(cup, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(cup, C_INK2, 0);
    lv_obj_set_style_border_width(cup, 2, 0);
    lv_obj_set_style_border_side(cup, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT |
                                      LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(cup, 4, 0);
    lv_obj_t *handle = ui_widgets_mk_box(icon, 7, 7);
    lv_obj_set_pos(handle, 12, 6);
    lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(handle, C_INK2, 0);
    lv_obj_set_style_border_width(handle, 2, 0);
    lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *saucer = ui_widgets_mk_box(icon, 18, 2);
    lv_obj_set_pos(saucer, 1, 16);
    lv_obj_set_style_bg_color(saucer, C_INK2, 0);
    lv_obj_set_style_radius(saucer, 1, 0);
    return icon;
}

void ui_player_build_screen_wake_layer(void)
{
    screen_wake_layer = ui_widgets_mk_box(lv_layer_top(), 320, 240);
    lv_obj_set_pos(screen_wake_layer, 0, 0);
    lv_obj_set_style_bg_opa(screen_wake_layer, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(screen_wake_layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(screen_wake_layer, on_screen_wake, LV_EVENT_PRESSED, NULL);
}

static void build_donation_dialog(void)
{
    donation_layer = ui_widgets_mk_box(scr_player, 320, 240);
    lv_obj_set_style_bg_color(donation_layer, C_INK, 0);
    lv_obj_set_style_bg_opa(donation_layer, 84, 0);
    lv_obj_add_flag(donation_layer, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE |
                                    LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(donation_layer, 0, 0);
    lv_obj_add_event_cb(donation_layer, on_donation_close, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = ui_widgets_mk_box(donation_layer, 216, 232);
    lv_obj_set_pos(card, 52, 4);
    lv_obj_set_style_bg_color(card, C_WHITE, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_t *title = ui_widgets_mk_label(card, "请我喝杯咖啡", &font_cjk_14, C_INK);
    lv_obj_set_pos(title, 16, 13);
    lv_obj_t *close = ui_widgets_mk_btn(card, LV_SYMBOL_CLOSE, &lv_font_montserrat_14,
                             false, on_donation_close, NULL);
    lv_obj_set_size(close, 44, 40);
    lv_obj_set_style_bg_opa(close, LV_OPA_TRANSP, 0);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *qr = lv_img_create(card);
    lv_img_set_src(qr, &donation_qr);
    lv_obj_set_pos(qr, 20, 44);
}

static void build_drawer(void)
{
    drawer_layer = ui_widgets_mk_box(scr_player, 320, 240);
    lv_obj_set_style_bg_opa(drawer_layer, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(drawer_layer, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(drawer_layer, 0, 0);
    lv_obj_add_flag(drawer_layer, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *drawer = ui_widgets_mk_box(drawer_layer, 214, 240);
    lv_obj_set_pos(drawer, 0, 0);
    lv_obj_set_style_bg_color(drawer, C_PAPER, 0);
    lv_obj_set_style_border_side(drawer, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(drawer, C_LINE, 0);
    lv_obj_set_style_border_width(drawer, 1, 0);
    lv_obj_set_style_shadow_width(drawer, 0, 0);

    lv_obj_t *head = ui_widgets_mk_box(drawer, 214, 48);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(head, C_LINE, 0);
    lv_obj_set_style_border_width(head, 1, 0);
    lv_obj_t *brand = ui_widgets_mk_label(head, "Radio OS", &lv_font_montserrat_18, C_INK);
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *author = ui_widgets_mk_label(head, "by BI8SYN", &lv_font_montserrat_12, C_INK2);
    lv_obj_align_to(author, brand, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -1);
    lv_obj_t *close = ui_widgets_mk_btn(head, LV_SYMBOL_CLOSE, &lv_font_montserrat_14, false,
                             on_drawer_close, NULL);
    lv_obj_set_size(close, 42, 46);
    lv_obj_set_style_bg_opa(close, LV_OPA_TRANSP, 0);
    lv_obj_align(close, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *quick = ui_widgets_mk_box(drawer, 214, 60);
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
    lv_obj_t *display_icon = ui_widgets_build_small_icon(screen_off, SMALL_ICON_DISPLAY_OFF);
    lv_obj_set_pos(display_icon, 9, 12);
    lv_obj_t *screen_name = ui_widgets_mk_label(screen_off, "息屏播放", &font_cjk_14, C_INK);
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
    lv_obj_t *sleep_icon = ui_widgets_mk_label(sleep, LV_SYMBOL_BELL, &lv_font_montserrat_14, C_INK2);
    lv_obj_set_pos(sleep_icon, 9, 15);
    drawer_sleep_label = ui_widgets_mk_label(sleep, "睡眠关闭", &font_cjk_14, C_INK);
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
    lv_obj_t *settings_icon = ui_widgets_mk_label(settings, LV_SYMBOL_SETTINGS, &lv_font_montserrat_14, C_INK2);
    lv_obj_set_pos(settings_icon, 13, 15);
    lv_obj_t *settings_name = ui_widgets_mk_label(settings, "设置", &font_cjk_14, C_INK);
    lv_obj_set_pos(settings_name, 42, 13);
    lv_obj_t *settings_arrow = ui_widgets_mk_label(settings, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, C_INK2);
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
    lv_obj_t *donate_name = ui_widgets_mk_label(donate, "请我喝杯咖啡", &font_cjk_14, C_INK);
    lv_obj_set_pos(donate_name, 42, 13);
    lv_obj_t *donate_arrow = ui_widgets_mk_label(donate, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, C_INK2);
    lv_obj_align(donate_arrow, LV_ALIGN_RIGHT_MID, -6, 0);

    lv_obj_t *footer = ui_widgets_mk_box(drawer, 214, 32);
    lv_obj_set_pos(footer, 0, 208);
    char footer_text[48];
    snprintf(footer_text, sizeof(footer_text), "Radio OS %s", ota_mgr_running_version());
    lv_obj_t *note = ui_widgets_mk_label(footer, footer_text, &lv_font_montserrat_12, C_INK2);
    lv_obj_center(note);

    lv_obj_t *scrim = ui_widgets_mk_box(drawer_layer, 106, 240);
    lv_obj_set_pos(scrim, 214, 0);
    lv_obj_set_style_bg_color(scrim, C_INK, 0);
    lv_obj_set_style_bg_opa(scrim, 72, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scrim, on_drawer_close, LV_EVENT_CLICKED, NULL);
    ui_player_update_sleep_label();
}

void ui_player_build(void)
{
    scr_player = lv_obj_create(NULL);
    ui_widgets_plain(scr_player);

    lv_obj_t *top = ui_widgets_mk_box(scr_player, 320, 40);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_t *menu = ui_widgets_mk_btn(top, LV_SYMBOL_BARS, &lv_font_montserrat_20, false,
                            on_drawer_open, NULL);
    lv_obj_set_size(menu, 44, 36);
    lv_obj_set_pos(menu, 6, 2);
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, 0);
    lv_obj_t *top_center = ui_widgets_mk_box(top, 176, 36);
    lv_obj_set_pos(top_center, 50, 2);
    lv_obj_set_flex_flow(top_center, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(top_center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(top_center, 0, 0);
    lbl_clock = ui_widgets_mk_label(top_center, "--:--", &font_cjk_14, C_INK2);
    top_sleep_group = ui_widgets_mk_box(top_center, 68, 16);
    lv_obj_set_flex_flow(top_sleep_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_sleep_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top_sleep_group, 4, 0);
    ui_widgets_build_small_icon(top_sleep_group, SMALL_ICON_TIMER);
    lbl_sleep_countdown = ui_widgets_mk_label(top_sleep_group, "15:00", &lv_font_montserrat_12, C_ACCENT);
    lv_obj_add_flag(top_sleep_group, LV_OBJ_FLAG_HIDDEN);
    btn_shuffle = ui_widgets_mk_btn(top, LV_SYMBOL_SHUFFLE, &lv_font_montserrat_20,
                                     false, on_shuffle, NULL);
    lv_obj_set_size(btn_shuffle, 44, 36);
    lv_obj_set_pos(btn_shuffle, 226, 2);
    lv_obj_set_style_radius(btn_shuffle, 10, 0);
    lv_obj_set_style_bg_opa(btn_shuffle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(btn_shuffle, C_INK2, 0);
    lv_obj_set_style_bg_opa(btn_shuffle, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(btn_shuffle, C_TINT, LV_STATE_CHECKED);
    lv_obj_t *shuffle_icon = lv_obj_get_child(btn_shuffle, 0);
    lv_obj_set_style_text_color(shuffle_icon, C_INK2, 0);
    // 子标签颜色显式跟随父按钮的选中态。
    lv_obj_set_style_text_color(btn_shuffle, C_ACCENT, LV_STATE_CHECKED);
    lv_obj_remove_local_style_prop(shuffle_icon, LV_STYLE_TEXT_COLOR, 0);
    if (app_radio_state()->shuffle) lv_obj_add_state(btn_shuffle, LV_STATE_CHECKED);
    lv_obj_t *wifi_btn = ui_widgets_mk_btn(top, LV_SYMBOL_WIFI, &lv_font_montserrat_20, false,
                                ui_wifi_on_wifi_open, NULL);
    lv_obj_set_size(wifi_btn, 44, 36);
    lv_obj_set_pos(wifi_btn, 270, 2);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_TRANSP, 0);
    lbl_wifi = lv_obj_get_child(wifi_btn, 0);
    lbl_offline = NULL;

    lv_obj_t *title = ui_widgets_mk_box(scr_player, 320, 66);
    lv_obj_set_pos(title, 0, 40);
    lbl_meta = ui_widgets_mk_label(title, "", &font_cjk_14, C_INK2);
    lv_obj_set_width(lbl_meta, 288);
    lv_obj_set_style_text_align(lbl_meta, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_meta, 16, 6);
    lbl_name = ui_widgets_mk_label(title, "未选择电台", &font_cjk_24, C_INK);
    lv_obj_set_size(lbl_name, 288, 31);
    lv_obj_set_pos(lbl_name, 16, 27);
    lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(lbl_name, LV_TEXT_ALIGN_CENTER, 0);

    home_visual = ui_visual_create(scr_player, 236, 44);
    lv_obj_set_pos(home_visual, 42, 100);

    lbl_state = ui_widgets_mk_label(scr_player, "已暂停", &font_cjk_14, C_INK2);
    lv_obj_set_size(lbl_state, 304, 18);
    lv_obj_set_pos(lbl_state, 8, 144);
    lv_obj_set_style_text_align(lbl_state, LV_TEXT_ALIGN_CENTER, 0);

    home_update_banner = ui_widgets_mk_box(scr_player, 304, 22);
    lv_obj_set_pos(home_update_banner, 8, 140);
    lv_obj_set_style_radius(home_update_banner, 8, 0);
    lv_obj_set_style_bg_color(home_update_banner, C_TINT, 0);
    lv_obj_set_style_bg_opa(home_update_banner, LV_OPA_COVER, 0);
    lv_obj_add_flag(home_update_banner, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_ext_click_area(home_update_banner, 10);
    lv_obj_add_event_cb(home_update_banner, ui_info_on_ota_open, LV_EVENT_CLICKED, NULL);
    home_update_label = ui_widgets_mk_label(home_update_banner, "发现新版本", &font_cjk_14, C_ACCENT);
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

    row_err = ui_widgets_mk_box(scr_player, 320, 46);
    lv_obj_set_pos(row_err, 0, 162);
    lv_obj_set_flex_flow(row_err, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_err, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row_err, 8, 0);
    lv_obj_add_flag(row_err, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *b_retry = ui_widgets_mk_btn(row_err, "重试", &font_cjk_14, true, on_retry_play, NULL);
    lv_obj_set_size(b_retry, 104, 40);
    lv_obj_t *b_nextx = ui_widgets_mk_btn(row_err, "下一台", &font_cjk_14, false, on_next, NULL);
    lv_obj_set_size(b_nextx, 104, 40);

    player_controls = ui_widgets_mk_box(scr_player, 320, 46);
    lv_obj_set_pos(player_controls, 0, 162);
    lv_obj_set_flex_flow(player_controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(player_controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(player_controls, 6, 0);
    lv_obj_t *buttons[5];
    buttons[0] = mk_player_icon_btn(player_controls, PLAYER_ICON_LIST, on_goto_list);
    buttons[1] = mk_player_icon_btn(player_controls, PLAYER_ICON_PREV, on_prev);
    buttons[2] = mk_player_icon_btn(player_controls, PLAYER_ICON_PLAY, on_toggle_play);
    buttons[3] = mk_player_icon_btn(player_controls, PLAYER_ICON_NEXT, on_next);
    buttons[4] = ui_widgets_mk_favorite_btn(player_controls, false, on_player_fav, NULL);
    for (int i = 0; i < 5; i++) {
        lv_obj_set_size(buttons[i], i == 2 ? 66 : 54, 44);
        lv_obj_set_style_bg_opa(buttons[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(buttons[i], LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(buttons[i], C_TINT, LV_STATE_PRESSED);
        /* 只缩命中区，不缩可视尺寸：图标和按下高亮都按对象坐标绘制，与命中区无关。
         * lv_coord_t 是有符号的，而 lv_obj_get_click_area() 做的是 x1 -= pad / x2 += pad，
         * 所以负值会向内收缩。54x44 的命中区变成 42x32，纵向从 y163-207 收到 y169-201：
         * 与下方音量带之间的惰性区从 7px 扩到 13px，相邻按钮之间也多出 18px 互不响应的
         * 空隙，顺带减少上一台/下一台之间的错点。*/
        lv_obj_set_ext_click_area(buttons[i], -6);
    }
    btn_player_play = buttons[2];
    btn_player_fav = buttons[4];

    lv_obj_t *vol = ui_widgets_mk_box(scr_player, 320, 26);
    lv_obj_set_pos(vol, 0, 214);
    lbl_vol_low = ui_widgets_mk_label(vol, LV_SYMBOL_MUTE, &lv_font_montserrat_14, C_INK2);
    lv_obj_set_pos(lbl_vol_low, 14, 5);
    lbl_vol_high = ui_widgets_mk_label(vol, LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_14, C_INK2);
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
    /* 滑块本体只有 4px 高，而指尖在这块屏上约 45px（320px / 约 57mm ≈ 5.6 px/mm）。
     * LVGL 的命中区就是对象矩形加 ext_click_area，所以不扩的话必须把触点落进那 4px
     * 窄条，往上偏一点就打到上面那行播放/换台按钮。扩 11px 让触摸带覆盖 y 214-240，
     * 视觉仍是原来的细线。（lv_slider.c 里那段"只测旋钮"的 HIT_TEST 处理是死代码：
     * 它要求对象带 LV_OBJ_FLAG_ADV_HITTEST，而 slider 与基类 bar 都从不设这个标志。）*/
    lv_obj_set_ext_click_area(sld_vol, 11);
    lv_obj_add_event_cb(sld_vol, on_vol_change, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sld_vol, on_vol_release, LV_EVENT_RELEASED, NULL);

    build_drawer();
    build_donation_dialog();
}

esp_err_t ui_set_drawer_visible(bool visible)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    if (visible) {
        ui_player_refresh();
        ui_navigate_locked(UI_SCREEN_PLAYER);
        ui_player_update_sleep_label();
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
        ui_player_refresh();
        ui_navigate_locked(UI_SCREEN_PLAYER);
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
    ui_player_set_display_awake_locked(awake);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_sleep_timer_seconds(int seconds)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    esp_err_t err = app_radio_set_sleep(seconds);
    ui_player_update_sleep_label();
    ui_player_update_sleep_countdown();
    lvgl_port_unlock();
    return err;
}

lv_obj_t *ui_player_screen(ui_screen_t id)
{
    switch (id) {
    case UI_SCREEN_PLAYER: return scr_player;
    default: return NULL;
    }
}

// 仅在已切到过渡屏幕后调用：父屏幕先销毁子控件，再清空本模块句柄。
void ui_player_destroy(void)
{
    if (scr_player) lv_obj_del(scr_player);
    if (screen_wake_layer) lv_obj_del(screen_wake_layer);
    ui_visual_destroy();
    scr_player = NULL;
    lbl_name = NULL;
    lbl_meta = NULL;
    lbl_state = NULL;
    btn_player_play = NULL;
    btn_player_fav = NULL;
    home_update_banner = NULL;
    home_update_label = NULL;
    row_err = NULL;
    lbl_wifi = NULL;
    lbl_offline = NULL;
    lbl_vol_low = NULL;
    lbl_vol_high = NULL;
    sld_vol = NULL;
    btn_shuffle = NULL;
    lbl_clock = NULL;
    top_sleep_group = NULL;
    lbl_sleep_countdown = NULL;
    player_controls = NULL;
    home_visual = NULL;
    drawer_layer = NULL;
    drawer_sleep_label = NULL;
    donation_layer = NULL;
    screen_wake_layer = NULL;
    s_play_icon_active = false;
    s_play_morph = 0;
}

void ui_player_tick(unsigned ticks)
{
    player_metrics_t m = {0};
    player_metrics(&m);
    if (lv_scr_act() == scr_player) ui_visual_render(m.bands, m.level);
    if (ticks % 25 == 0 && lbl_clock) {
        time_t now = time(NULL);
        struct tm local;
        char text[20] = "--:--";
        if (now > 1700000000 && localtime_r(&now, &local)) {
            if (app_radio_state()->time_24h) strftime(text, sizeof(text), "%H:%M", &local);
            else snprintf(text, sizeof(text), "%s %d:%02d", local.tm_hour < 12 ? "上午" : "下午",
                          local.tm_hour % 12 ? local.tm_hour % 12 : 12, local.tm_min);
        }
        lv_label_set_text(lbl_clock, text);
        ui_player_update_sleep_countdown();
    }
}
