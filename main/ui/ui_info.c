// 关于、更新、诊断页按需创建并在离开时释放；触摸测试页保留自己的采样状态。
#include "ui_internal.h"
static const char *TAG = "ui_info";

static bool is_info_screen(lv_obj_t *screen);
static void position_touch_object(lv_obj_t *object, lv_coord_t x, lv_coord_t y,
                                  lv_coord_t size);
static void on_touch_test_restart(lv_event_t *e);
static void on_touch_test_sample(lv_event_t *e);
static void on_touch_test_back(lv_event_t *e);
static void on_touch_test_open(lv_event_t *e);
static void on_ota_start(lv_event_t *e);

static lv_obj_t *scr_about, *scr_ota, *scr_diagnostics, *scr_touch_test;
static lv_obj_t *ota_body, *diagnostics_body;
static lv_obj_t *touch_target, *touch_dot, *touch_status, *touch_detail, *touch_restart;
static int s_touch_step, s_touch_dx[5], s_touch_dy[5];
static const lv_point_t s_touch_targets[5] = {{28,92},{292,92},{160,145},{28,215},{292,215}};
static bool is_info_screen(lv_obj_t *screen)
{
    return screen && (screen == scr_about || screen == scr_ota || screen == scr_diagnostics);
}

void ui_info_release(lv_obj_t *screen)
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

void ui_info_on_about_open(lv_event_t *e)
{
    (void)e;
    if (!scr_about) ui_info_build_about();
    ui_navigate_locked(UI_SCREEN_ABOUT);
}

void ui_info_on_diagnostics_open(lv_event_t *e)
{
    (void)e;
    if (!scr_diagnostics) ui_info_build_diagnostics_screen();
    ui_info_refresh_diagnostics_screen();
    ui_navigate_locked(UI_SCREEN_DIAGNOSTICS);
}

void ui_info_build_about(void)
{
    scr_about = lv_obj_create(NULL);
    ui_widgets_plain(scr_about);
    lv_obj_set_flex_flow(scr_about, LV_FLEX_FLOW_COLUMN);
    ui_widgets_build_page_header(scr_about, "关于");

    lv_obj_t *body = ui_widgets_mk_box(scr_about, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(body, 1);

    lv_obj_t *identity = ui_widgets_mk_box(body, 230, 52);
    lv_obj_align(identity, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_t *mark = ui_widgets_mk_box(identity, 44, 44);
    lv_obj_set_style_bg_color(mark, C_TINT, 0);
    lv_obj_set_style_radius(mark, 13, 0);
    lv_obj_align(mark, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *mark_icon = ui_widgets_mk_label(mark, LV_SYMBOL_AUDIO, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_center(mark_icon);
    lv_obj_t *brand = ui_widgets_mk_label(identity, "Radio OS", &lv_font_montserrat_20, C_INK);
    lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 56, 2);
    lv_obj_t *tagline = ui_widgets_mk_label(identity, "专注、纯粹的网络收音机", &font_cjk_14, C_INK2);
    lv_obj_align(tagline, LV_ALIGN_BOTTOM_LEFT, 56, -2);

    lv_obj_t *card = ui_widgets_mk_box(body, 288, 92);
    lv_obj_set_style_bg_color(card, C_SURFACE, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_color(card, C_LINE, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_t *divider_v = ui_widgets_mk_box(card, 1, 50);
    lv_obj_set_style_bg_color(divider_v, C_LINE, 0);
    lv_obj_align(divider_v, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *divider_h = ui_widgets_mk_box(card, 288, 1);
    lv_obj_set_style_bg_color(divider_h, C_LINE, 0);
    lv_obj_align(divider_h, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t *vlabel = ui_widgets_mk_label(card, "当前版本", &font_cjk_14, C_INK2);
    lv_obj_align(vlabel, LV_ALIGN_TOP_LEFT, 51, 6);
    lv_obj_t *version = ui_widgets_mk_label(card, ota_mgr_running_version(), &lv_font_montserrat_14, C_INK);
    lv_obj_align(version, LV_ALIGN_TOP_LEFT, 55, 25);
    lv_obj_t *clabel = ui_widgets_mk_label(card, "本机电台", &font_cjk_14, C_INK2);
    lv_obj_align(clabel, LV_ALIGN_TOP_RIGHT, -51, 6);
    char count[12];
    snprintf(count, sizeof(count), "%d", stations_catalog_count());
    lv_obj_t *catalog = ui_widgets_mk_label(card, count, &lv_font_montserrat_14, C_INK);
    lv_obj_align(catalog, LV_ALIGN_TOP_RIGHT, -60, 25);
    lv_obj_t *by = ui_widgets_mk_label(card, "设计与开发", &font_cjk_14, C_INK2);
    lv_obj_align(by, LV_ALIGN_BOTTOM_LEFT, 16, -11);
    lv_obj_t *author = ui_widgets_mk_label(card, "BI8SYN", &lv_font_montserrat_14, C_ACCENT);
    lv_obj_align(author, LV_ALIGN_BOTTOM_RIGHT, -16, -11);
}

static void position_touch_object(lv_obj_t *object, lv_coord_t x, lv_coord_t y,
                                  lv_coord_t size)
{
    lv_obj_set_pos(object, x - size / 2, y - size / 2);
}

void ui_info_reset_touch_test(void)
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
    ui_info_reset_touch_test();
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
    if (!scr_diagnostics) ui_info_build_diagnostics_screen();
    ui_info_refresh_diagnostics_screen();
    ui_navigate_locked(UI_SCREEN_DIAGNOSTICS);
}

static void on_touch_test_open(lv_event_t *e)
{
    (void)e;
    ui_info_reset_touch_test();
    ui_navigate_locked(UI_SCREEN_TOUCH_TEST);
}

void ui_info_build_touch_test(void)
{
    scr_touch_test = lv_obj_create(NULL);
    ui_widgets_plain(scr_touch_test);

    lv_obj_t *header = ui_widgets_mk_box(scr_touch_test, LV_PCT(100), HEADER_H);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, C_LINE, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_t *back = ui_widgets_mk_btn(header, LV_SYMBOL_LEFT, &lv_font_montserrat_14,
                            false, on_touch_test_back, NULL);
    lv_obj_set_size(back, 48, HEADER_H - 1);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *title = ui_widgets_mk_label(header, "触摸测试", &font_cjk_18, C_INK);
    lv_obj_center(title);

    lv_obj_t *capture = ui_widgets_mk_box(scr_touch_test, LV_PCT(100), 240 - HEADER_H);
    lv_obj_set_pos(capture, 0, HEADER_H);
    lv_obj_add_flag(capture, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(capture, on_touch_test_sample, LV_EVENT_RELEASED, NULL);

    touch_status = ui_widgets_mk_label(capture, "", &font_cjk_14, C_INK);
    lv_obj_set_width(touch_status, 300);
    lv_obj_set_style_text_align(touch_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(touch_status, LV_ALIGN_TOP_MID, 0, 4);
    touch_detail = ui_widgets_mk_label(capture, "", &font_cjk_14, C_INK2);
    lv_obj_set_width(touch_detail, 304);
    lv_label_set_long_mode(touch_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(touch_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(touch_detail, LV_ALIGN_TOP_MID, 0, 24);

    touch_target = ui_widgets_mk_box(scr_touch_test, 28, 28);
    lv_obj_set_style_bg_opa(touch_target, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch_target, 2, 0);
    lv_obj_set_style_border_color(touch_target, C_ACCENT, 0);
    lv_obj_set_style_radius(touch_target, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *target_h = ui_widgets_mk_box(touch_target, 20, 2);
    lv_obj_set_style_bg_color(target_h, C_ACCENT, 0);
    lv_obj_center(target_h);
    lv_obj_t *target_v = ui_widgets_mk_box(touch_target, 2, 20);
    lv_obj_set_style_bg_color(target_v, C_ACCENT, 0);
    lv_obj_center(target_v);
    lv_obj_clear_flag(touch_target, LV_OBJ_FLAG_CLICKABLE);
    // 父对象不接收点击，并不代表子对象也不接收。十字线只是装饰，
    // 必须一起穿透，否则准确点中标记中心反而不会触发下方采样区。
    lv_obj_clear_flag(target_h, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(target_v, LV_OBJ_FLAG_CLICKABLE);

    touch_dot = ui_widgets_mk_box(scr_touch_test, 8, 8);
    lv_obj_set_style_bg_color(touch_dot, C_ACCENT, 0);
    lv_obj_set_style_radius(touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(touch_dot, LV_OBJ_FLAG_CLICKABLE);

    touch_restart = ui_widgets_mk_btn(capture, "重新测试", &font_cjk_14, true,
                           on_touch_test_restart, NULL);
    lv_obj_set_size(touch_restart, 128, 44);
    lv_obj_align(touch_restart, LV_ALIGN_BOTTOM_MID, 0, -16);
    ui_info_reset_touch_test();
}

void ui_info_build_diagnostics_screen(void)
{
    scr_diagnostics = lv_obj_create(NULL);
    ui_widgets_plain(scr_diagnostics);
    lv_obj_set_flex_flow(scr_diagnostics, LV_FLEX_FLOW_COLUMN);
    ui_widgets_build_page_header(scr_diagnostics, "诊断");
    diagnostics_body = ui_widgets_mk_box(scr_diagnostics, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(diagnostics_body, 1);
    lv_obj_set_flex_flow(diagnostics_body, LV_FLEX_FLOW_COLUMN);
    ui_info_refresh_diagnostics_screen();
}

void ui_info_refresh_diagnostics_screen(void)
{
    if (!diagnostics_body) return;
    lv_obj_clean(diagnostics_body);
    char mem[32];
    snprintf(mem, sizeof(mem), "%u KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    ui_widgets_setting_nav_row(diagnostics_body, "网络", app_radio_state()->wifi_connected ? "已连接" : "未连接", NULL, NULL);
    ui_widgets_setting_nav_row(diagnostics_body, "音频", board_codec() ? "正常" : "不可用", NULL, NULL);
    char catalog[16]; snprintf(catalog, sizeof(catalog), "%d", stations_catalog_count());
    ui_widgets_setting_nav_row(diagnostics_body, "电台目录", catalog, NULL, NULL);
    ui_widgets_setting_nav_row(diagnostics_body, "内部内存", mem, NULL, NULL);
    ui_widgets_setting_nav_row(diagnostics_body, "触摸测试", "五点检测", on_touch_test_open, NULL);
}

static void on_ota_start(lv_event_t *e)
{
    (void)e;
    if (!app_radio_state()->wifi_connected) {
        ota_mgr_status_t status = {
            .state = OTA_MGR_ERROR,
            .progress = 0,
        };
        snprintf(status.message, sizeof(status.message), "请先连接 Wi-Fi");
        // 页面直接重绘离线状态；联网后再次点击即可。
        lv_obj_clean(ota_body);
        lv_obj_t *message = ui_widgets_mk_label(ota_body, status.message, &font_cjk_18, C_INK);
        lv_obj_align(message, LV_ALIGN_CENTER, 0, -8);
        lv_obj_t *hint = ui_widgets_mk_label(ota_body, "返回设置连接网络后重试", &font_cjk_14, C_INK2);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 22);
        return;
    }
    ota_mgr_status_t current = { 0 };
    ota_mgr_status(&current);
    bool install = current.state == OTA_MGR_UPDATE_AVAILABLE;
    // 检查更新不打断收听，只有用户确认安装时才释放播放器资源。
    if (install) player_stop();
    esp_err_t err = install ? ota_mgr_start(ui_on_ota_status, NULL)
                            : ota_mgr_check(ui_on_ota_status, NULL);
    if (err != ESP_OK) ui_info_refresh_ota();
}

void ui_info_refresh_ota(void)
{
    if (!ota_body) return;
    lv_obj_clean(ota_body);
    ota_mgr_status_t status = { 0 };
    ota_mgr_status(&status);
    bool busy = status.state == OTA_MGR_CHECKING || status.state == OTA_MGR_DOWNLOADING;

    lv_obj_t *icon_box = ui_widgets_mk_box(ota_body, 40, 40);
    lv_obj_set_style_bg_color(icon_box, C_TINT, 0);
    lv_obj_set_style_radius(icon_box, 12, 0);
    lv_obj_align(icon_box, LV_ALIGN_TOP_LEFT, 28, 10);
    const char *icon_text = status.state == OTA_MGR_UP_TO_DATE ||
                            status.state == OTA_MGR_READY_TO_RESTART
                            ? LV_SYMBOL_OK : (busy ? LV_SYMBOL_REFRESH : LV_SYMBOL_DOWNLOAD);
    lv_obj_t *icon = ui_widgets_mk_label(icon_box, icon_text, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_center(icon);

    const char *title_text = "在线更新";
    if (status.state == OTA_MGR_CHECKING) title_text = "正在检查更新";
    else if (status.state == OTA_MGR_UPDATE_AVAILABLE) title_text = "发现新版本";
    else if (status.state == OTA_MGR_DOWNLOADING) title_text = "正在下载更新";
    else if (status.state == OTA_MGR_UP_TO_DATE) title_text = "已是最新版本";
    else if (status.state == OTA_MGR_READY_TO_RESTART) title_text = "更新完成";
    else if (status.state == OTA_MGR_UNAVAILABLE || status.state == OTA_MGR_ERROR)
        title_text = "暂时无法更新";
    lv_obj_t *title = ui_widgets_mk_label(ota_body, title_text, &font_cjk_18, C_INK);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 84, 8);

    char version_text[72];
    if (status.state == OTA_MGR_UPDATE_AVAILABLE)
        snprintf(version_text, sizeof(version_text), "当前 %s  ·  最新 %s",
                 ota_mgr_running_version(), status.available_version);
    else
        snprintf(version_text, sizeof(version_text), "当前版本  %s", ota_mgr_running_version());
    lv_obj_t *version = ui_widgets_mk_label(ota_body, version_text, &font_cjk_14, C_INK2);
    lv_obj_align(version, LV_ALIGN_TOP_LEFT, 84, 32);

    const char *copy_text = status.message[0] ? status.message :
                            "通过 Wi-Fi 安全下载并校验新固件";
    lv_obj_t *copy = ui_widgets_mk_label(ota_body, copy_text, &font_cjk_14, C_INK2);
    lv_obj_set_width(copy, 288);
    lv_obj_set_style_text_align(copy, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(copy, LV_ALIGN_TOP_MID, 0, 58);

    if (status.state == OTA_MGR_UPDATE_AVAILABLE && status.release_notes[0]) {
        lv_obj_t *notes_title = ui_widgets_mk_label(ota_body, "更新内容", &font_cjk_14, C_INK);
        lv_obj_set_pos(notes_title, 16, 82);
        lv_obj_t *notes = ui_widgets_mk_label(ota_body, status.release_notes, &font_cjk_14, C_INK2);
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
        lv_obj_t *check = ui_widgets_mk_btn(ota_body, button_text, &font_cjk_14, true, on_ota_start, NULL);
        lv_obj_set_size(check, 160, 40);
        lv_obj_align(check, LV_ALIGN_TOP_MID, 0,
                     status.state == OTA_MGR_UPDATE_AVAILABLE ? 146 : 102);
    }
}

void ui_info_on_ota_open(lv_event_t *e)
{
    (void)e;
    if (!scr_ota) ui_info_build_ota_screen();
    ui_navigate_locked(UI_SCREEN_OTA);
    ui_info_refresh_ota();
}

void ui_info_build_ota_screen(void)
{
    scr_ota = lv_obj_create(NULL);
    ui_widgets_plain(scr_ota);
    lv_obj_set_flex_flow(scr_ota, LV_FLEX_FLOW_COLUMN);
    ui_widgets_build_page_header(scr_ota, "系统更新");
    ota_body = ui_widgets_mk_box(scr_ota, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(ota_body, 1);
    ui_info_refresh_ota();
}

lv_obj_t *ui_info_screen(ui_screen_t id)
{
    switch (id) {
    case UI_SCREEN_ABOUT: return scr_about;
    case UI_SCREEN_OTA: return scr_ota;
    case UI_SCREEN_DIAGNOSTICS: return scr_diagnostics;
    case UI_SCREEN_TOUCH_TEST: return scr_touch_test;
    default: return NULL;
    }
}

// 仅在已切到过渡屏幕后调用：父屏幕先销毁子控件，再清空本模块句柄。
void ui_info_destroy(void)
{
    if (scr_about) lv_obj_del(scr_about);
    if (scr_ota) lv_obj_del(scr_ota);
    if (scr_diagnostics) lv_obj_del(scr_diagnostics);
    if (scr_touch_test) lv_obj_del(scr_touch_test);
    scr_about = NULL;
    scr_ota = NULL;
    scr_diagnostics = NULL;
    scr_touch_test = NULL;
    ota_body = NULL;
    diagnostics_body = NULL;
    touch_target = NULL;
    touch_dot = NULL;
    touch_status = NULL;
    touch_detail = NULL;
    touch_restart = NULL;
}
