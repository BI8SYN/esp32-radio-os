// 配网页拥有输入框和扫描列表；工作任务只发布数据，不能持有页面控件指针。
#include "ui_internal.h"
static const char *TAG = "ui_wifi";

static void wifi_scan_task(void *arg);
static void on_wifi_rescan(lv_event_t *e);
static void on_wifi_back(lv_event_t *e);
static void on_wifi_password_back(lv_event_t *e);
static void on_wifi_submit(lv_event_t *e);
static void on_wifi_field_focus(lv_event_t *e);
static void log_wifi_password_layout(void);
static void on_wifi_row(lv_event_t *e);
static void refresh_wifi_list(void);
static lv_obj_t *setup_screen(void);
static lv_obj_t *setup_line(lv_obj_t *par, const char *txt, const lv_font_t *f, lv_color_t c);

static lv_obj_t *scr_wifi, *scr_wifi_pass, *scr_setup;
static lv_obj_t *wifi_content, *wifi_ssid_input, *wifi_password, *wifi_keyboard;
static wifi_mgr_ap_t s_wifi_items[WIFI_MGR_SCAN_MAX];
static size_t s_wifi_count;
static int s_wifi_selected = -1;
static bool s_wifi_scanning, s_wifi_visible;
static void wifi_scan_task(void *arg)
{
    (void)arg;
    wifi_mgr_ap_t items[WIFI_MGR_SCAN_MAX];
    size_t count = 0;
    esp_err_t err = wifi_mgr_scan(items, WIFI_MGR_SCAN_MAX, &count);
    // 只发布扫描数据，不跨锁携带页面句柄。页面离开/主题重建后直接丢弃显示结果。
    while (!lvgl_port_lock(1000)) vTaskDelay(1);
    s_wifi_scanning = false;
    if (s_wifi_visible && wifi_content) {
        s_wifi_count = err == ESP_OK ? count : 0;
        memcpy(s_wifi_items, items, s_wifi_count * sizeof(items[0]));
        refresh_wifi_list();
    }
    lvgl_port_unlock();
    vTaskDelete(NULL);
}

void ui_wifi_start_scan(void)
{
    s_wifi_visible = true;
    if (s_wifi_scanning) return;
    s_wifi_scanning = true;
    s_wifi_count = 0;
    lv_obj_clean(wifi_content);
    ui_widgets_add_statebox(wifi_content, "正在扫描附近网络…", "只显示 2.4GHz Wi-Fi", NULL, NULL, NULL, NULL);
    if (xTaskCreate(wifi_scan_task, "ui_wifi_scan", 4096, NULL, 4, NULL) != pdPASS) {
        s_wifi_scanning = false;
        lv_obj_clean(wifi_content);
        ui_widgets_add_statebox(wifi_content, "扫描暂时不可用", "设备内存不足，请稍后重试",
                     "重试", on_wifi_rescan, NULL, NULL);
    }
}

static void on_wifi_rescan(lv_event_t *e)
{
    (void)e;
    ui_wifi_start_scan();
}

static void on_wifi_back(lv_event_t *e)
{
    (void)e;
    s_wifi_visible = false;
    s_wifi_count = 0;
    lv_obj_clean(wifi_content);  // 扫描行仅在此页需要，离开即归还紧张的内部 RAM
    ui_navigate_locked(UI_SCREEN_SETTINGS);
}

static void on_wifi_password_back(lv_event_t *e)
{
    (void)e;
    s_wifi_visible = true;
    refresh_wifi_list();
    ui_navigate_locked(UI_SCREEN_WIFI);
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
        ui_navigate_locked(UI_SCREEN_WIFI_PASSWORD);
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
    ui_navigate_locked(UI_SCREEN_WIFI_PASSWORD);
}

static void refresh_wifi_list(void)
{
    if (!wifi_content) return;
    lv_obj_clean(wifi_content);
    if (s_wifi_count == 0) {
        ui_widgets_add_statebox(wifi_content, "没有找到可用网络", "请靠近路由器后重试",
                     "重新扫描", on_wifi_rescan, NULL, NULL);
        lv_obj_t *manual = ui_widgets_mk_btn(wifi_content, "隐藏网络 / 手动输入", &font_cjk_14, false,
                                  on_wifi_row, (void *)(intptr_t)-1);
        lv_obj_set_size(manual, LV_PCT(100), 44);
        return;
    }
    char current[33];
    wifi_mgr_current_ssid(current, sizeof(current));
    for (int i = 0; i < (int)s_wifi_count; i++) {
        lv_obj_t *row = ui_widgets_mk_box(wifi_content, LV_PCT(100), 48);
        lv_obj_set_style_bg_color(row, C_SURFACE, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_left(row, 12, 0);
        lv_obj_set_style_pad_right(row, 12, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_wifi_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *name = ui_widgets_mk_label(row, s_wifi_items[i].ssid, &font_cjk_18, C_INK);
        lv_obj_set_width(name, 220);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
        const char *state = strcmp(current, s_wifi_items[i].ssid) == 0 && app_radio_state()->wifi_connected ? "已连接" :
                            (s_wifi_items[i].rssi > -58 ? "强" : (s_wifi_items[i].rssi > -72 ? "中" : "弱"));
        lv_obj_t *sig = ui_widgets_mk_label(row, state, &font_cjk_14,
                                 strcmp(state, "已连接") == 0 ? C_ACCENT : C_INK2);
        lv_obj_align(sig, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    lv_obj_t *manual = ui_widgets_mk_btn(wifi_content, "隐藏网络 / 手动输入", &font_cjk_14, false,
                              on_wifi_row, (void *)(intptr_t)-1);
    lv_obj_set_size(manual, LV_PCT(100), 44);
}

void ui_wifi_on_wifi_open(lv_event_t *e)
{
    (void)e;
    ui_navigate_locked(UI_SCREEN_WIFI);
    ui_wifi_start_scan();
}

void ui_wifi_build(void)
{
    scr_wifi = lv_obj_create(NULL);
    ui_widgets_plain(scr_wifi);
    lv_obj_set_flex_flow(scr_wifi, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *hdr = ui_widgets_mk_box(scr_wifi, LV_PCT(100), 40);
    lv_obj_t *back = ui_widgets_mk_btn(hdr, LV_SYMBOL_LEFT, &lv_font_montserrat_14, false, on_wifi_back, NULL);
    lv_obj_set_size(back, 48, 40); lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_t *title = ui_widgets_mk_label(hdr, "选择 Wi-Fi", &font_cjk_18, C_INK); lv_obj_center(title);
    lv_obj_t *scan = ui_widgets_mk_btn(hdr, "刷新", &font_cjk_14, false, on_wifi_rescan, NULL);
    lv_obj_set_size(scan, 56, 40); lv_obj_set_style_border_width(scan, 0, 0);
    lv_obj_align(scan, LV_ALIGN_RIGHT_MID, 0, 0);
    wifi_content = ui_widgets_mk_box(scr_wifi, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(wifi_content, 1);
    lv_obj_set_flex_flow(wifi_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(wifi_content, 8, 0);
    lv_obj_set_style_pad_row(wifi_content, 8, 0);
    lv_obj_add_flag(wifi_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(wifi_content, LV_DIR_VER);

    scr_wifi_pass = lv_obj_create(NULL);
    ui_widgets_plain(scr_wifi_pass);
    lv_obj_set_flex_flow(scr_wifi_pass, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr_wifi_pass, 0, 0);

    lv_obj_t *pass_header = ui_widgets_mk_box(scr_wifi_pass, LV_PCT(100), 36);
    lv_obj_t *pback = ui_widgets_mk_btn(pass_header, LV_SYMBOL_LEFT, &lv_font_montserrat_14, false,
                             on_wifi_password_back, NULL);
    lv_obj_set_size(pback, 48, 36); lv_obj_set_style_border_width(pback, 0, 0);
    lv_obj_set_style_bg_opa(pback, LV_OPA_TRANSP, 0);
    lv_obj_t *ptitle = ui_widgets_mk_label(pass_header, "连接 Wi-Fi", &font_cjk_18, C_INK);
    lv_obj_set_width(ptitle, 240); lv_obj_set_style_text_align(ptitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ptitle, 40, 6);

    // 输入区与键盘是连续操作面：由 flex 计算位置，不留任何不可点的中间空白。
    lv_obj_t *pass_form = ui_widgets_mk_box(scr_wifi_pass, LV_PCT(100), 64);
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

static lv_obj_t *setup_screen(void)
{
    if (!scr_setup) {
        scr_setup = lv_obj_create(NULL);
        ui_widgets_plain(scr_setup);
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
    lv_obj_t *l = ui_widgets_mk_label(par, txt, f, c);
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

    lv_obj_t *local = ui_widgets_mk_btn(s, "在本机选择 Wi-Fi", &font_cjk_18, true, ui_wifi_on_wifi_open, NULL);
    lv_obj_set_size(local, 240, 44);
    setup_line(s, "仅支持 2.4GHz 网络", &font_cjk_14, C_INK2);

    snprintf(buf, sizeof(buf), "备用：手机连 %s，再打开 %s", ap_ssid, ip);
    setup_line(s, buf, &font_cjk_14, C_INK2);

    ui_load_screen_locked(s);
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

    ui_load_screen_locked(s);
    lvgl_port_unlock();
}

void ui_hide_setup(void)
{
    if (!lvgl_port_lock(2000)) return;
    ui_player_refresh();
    ui_navigate_locked(UI_SCREEN_PLAYER);
    lvgl_port_unlock();
}

lv_obj_t *ui_wifi_screen(ui_screen_t id)
{
    switch (id) {
    case UI_SCREEN_WIFI: return scr_wifi;
    case UI_SCREEN_WIFI_PASSWORD: return scr_wifi_pass;
    case UI_SCREEN_SETUP: return scr_setup;
    default: return NULL;
    }
}

// 仅在已切到过渡屏幕后调用：父屏幕先销毁子控件，再清空本模块句柄。
void ui_wifi_destroy(void)
{
    s_wifi_visible = false;
    s_wifi_count = 0;
    if (scr_wifi) lv_obj_del(scr_wifi);
    if (scr_wifi_pass) lv_obj_del(scr_wifi_pass);
    if (scr_setup) lv_obj_del(scr_setup);
    scr_wifi = NULL;
    scr_wifi_pass = NULL;
    scr_setup = NULL;
    wifi_content = NULL;
    wifi_ssid_input = NULL;
    wifi_password = NULL;
    wifi_keyboard = NULL;
}

void ui_wifi_leave(void)
{
    s_wifi_visible = false;
    // 工作任务使用局部扫描缓冲，不会依赖这些将被释放的列表控件。
    s_wifi_count = 0;
    if (wifi_content) lv_obj_clean(wifi_content);
}
void ui_wifi_prepare_password(void)
{
    lv_keyboard_set_textarea(wifi_keyboard, wifi_password);
    log_wifi_password_layout();
}
