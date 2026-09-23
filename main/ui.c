#include "ui/ui_internal.h"
static const char *TAG = "ui_core";

static void on_player_state(player_state_t state, const char *message);
static void on_wifi_event(wifi_mgr_event_t event, const char *ssid);
static int active_screen_id(void);
static void build_pages(void);
static bool can_rebuild(void);
static void rebuild_ui_for_theme(void *unused);
static void ui_live_timer(lv_timer_t *timer);

// 应用回调只更新邮箱；LVGL 对象与应用状态通过 UI 任务或界面锁串行修改。
// 不在音频/Wi-Fi 工作任务里等待界面锁，避免慢页面拖住流媒体任务。
typedef struct {
    bool player_dirty, ota_dirty, wifi_dirty, connected;
    bool setup_dirty;
    wifi_mgr_event_t setup_event;
    char ssid[33];
} ui_mailbox_t;
static ui_mailbox_t s_mailbox;
static portMUX_TYPE s_mailbox_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_boot_ready, s_theme_rebuild_pending, s_theme_rebuild_scheduled;
static unsigned s_live_ticks;

static void on_player_state(player_state_t state, const char *message)
{
    (void)state; (void)message;
    portENTER_CRITICAL(&s_mailbox_lock);
    s_mailbox.player_dirty = true;
    portEXIT_CRITICAL(&s_mailbox_lock);
}

void ui_on_ota_status(const ota_mgr_status_t *status, void *user_data)
{
    (void)status; (void)user_data;
    portENTER_CRITICAL(&s_mailbox_lock);
    s_mailbox.ota_dirty = true;
    portEXIT_CRITICAL(&s_mailbox_lock);
}

static void on_wifi_event(wifi_mgr_event_t event, const char *ssid)
{
    portENTER_CRITICAL(&s_mailbox_lock);
    if (event == WIFI_MGR_CONNECTED || event == WIFI_MGR_DISCONNECTED) {
        s_mailbox.connected = event == WIFI_MGR_CONNECTED;
        s_mailbox.wifi_dirty = true;
    } else {
        s_mailbox.setup_dirty = true;
        s_mailbox.setup_event = event;
        strlcpy(s_mailbox.ssid, ssid ? ssid : "", sizeof(s_mailbox.ssid));
    }
    portEXIT_CRITICAL(&s_mailbox_lock);
}

lv_obj_t *ui_page_screen(ui_screen_t id)
{
    lv_obj_t *screen;
    if ((screen = ui_player_screen(id))) return screen;
    if ((screen = ui_stations_screen(id))) return screen;
    if ((screen = ui_settings_screen(id))) return screen;
    if ((screen = ui_wifi_screen(id))) return screen;
    if ((screen = ui_info_screen(id))) return screen;
    return ui_boot_screen(id);
}

static int active_screen_id(void)
{
    lv_obj_t *active = lv_scr_act();
    for (int id = UI_SCREEN_PLAYER; id <= UI_SCREEN_BOOT; ++id) {
        if (active && active == ui_page_screen(id)) return id;
    }
    return -1;
}

void ui_load_screen_locked(lv_obj_t *target)
{
    lv_obj_t *previous = lv_scr_act();
    if (previous == ui_page_screen(UI_SCREEN_WIFI) && target != previous &&
        target != ui_page_screen(UI_SCREEN_WIFI_PASSWORD)) ui_wifi_leave();
    lv_scr_load(target);
    if (previous != target) ui_info_release(previous);
}

// 触摸操作和串口诊断共用同一入口，离开页面时的清理也只维护一份。
esp_err_t ui_navigate_locked(ui_screen_t screen)
{
    if (screen < UI_SCREEN_PLAYER || screen > UI_SCREEN_TOUCH_TEST) return ESP_ERR_INVALID_ARG;
    switch (screen) {
    case UI_SCREEN_PLAYER: ui_player_refresh(); break;
    case UI_SCREEN_STATIONS: ui_stations_refresh_list(); break;
    case UI_SCREEN_SETTINGS: ui_settings_network_changed(); break;
    case UI_SCREEN_FILTER: ui_stations_prepare_filter(); break;
    case UI_SCREEN_WIFI: ui_wifi_start_scan(); break;
    case UI_SCREEN_WIFI_PASSWORD: ui_wifi_prepare_password(); break;
    case UI_SCREEN_ABOUT:
        if (!ui_page_screen(screen)) ui_info_build_about();
        break;
    case UI_SCREEN_OTA:
        if (!ui_page_screen(screen)) ui_info_build_ota_screen();
        ui_info_refresh_ota();
        break;
    case UI_SCREEN_DIAGNOSTICS:
        if (!ui_page_screen(screen)) ui_info_build_diagnostics_screen();
        ui_info_refresh_diagnostics_screen();
        break;
    case UI_SCREEN_TOUCH_TEST: ui_info_reset_touch_test(); break;
    default: break;
    }
    lv_obj_t *target = ui_page_screen(screen);
    if (!target) return ESP_ERR_INVALID_STATE;
    ui_load_screen_locked(target);
    return ESP_OK;
}

void ui_back_to_settings(lv_event_t *event)
{
    (void)event;
    ui_navigate_locked(UI_SCREEN_SETTINGS);
}

static void build_pages(void)
{
    ui_wifi_build();
    ui_settings_build();
    ui_info_build_touch_test();
    ui_player_build();
    ui_stations_build();
    ui_stations_build_filter();
    ui_player_build_screen_wake_layer();
    ui_player_refresh();
    ui_player_refresh_wifi();
}

static bool can_rebuild(void)
{
    int active = active_screen_id();
    // 保留配网输入、触摸测试和启动动画；稍后回到常规页面再应用主题。
    return s_boot_ready && app_radio_state()->screen_awake && active >= 0 &&
           active != UI_SCREEN_WIFI && active != UI_SCREEN_WIFI_PASSWORD &&
           active != UI_SCREEN_SETUP && active != UI_SCREEN_TOUCH_TEST && active != UI_SCREEN_BOOT;
}

static void rebuild_ui_for_theme(void *unused)
{
    (void)unused;
    s_theme_rebuild_scheduled = false;
    // 异步调用执行前用户可能已经跳页，必须重新检查，不能使用预约时的页面 ID。
    if (!can_rebuild()) { s_theme_rebuild_pending = true; return; }
    ui_screen_t target = active_screen_id();
    int scroll = ui_settings_scroll_locked();
    lv_obj_t *transition = lv_obj_create(NULL);
    ui_widgets_plain(transition);
    lv_obj_center(ui_widgets_mk_label(transition, "正在应用外观…", &font_cjk_14, C_INK2));
    lv_scr_load(transition);

    ui_player_destroy();
    ui_stations_destroy();
    ui_settings_destroy();
    ui_wifi_destroy();
    ui_info_destroy();
    build_pages();
    ui_navigate_locked(target);
    if (target == UI_SCREEN_SETTINGS) ui_settings_restore_scroll(scroll);
    lv_obj_del(transition);
    s_theme_rebuild_pending = false;
    ESP_LOGI(TAG, "外观已应用：mode=%d dark=%d accent=%d schedule=%02d-%02d",
             app_radio_state()->theme_mode, app_radio_state()->theme_dark,
             app_radio_state()->theme_accent, app_radio_state()->dark_start_hour,
             app_radio_state()->dark_end_hour);
}

void ui_request_theme_rebuild(void)
{
    s_theme_rebuild_pending = true;
    if (s_theme_rebuild_scheduled || !can_rebuild()) return;
    if (lv_async_call(rebuild_ui_for_theme, NULL) == LV_RES_OK) s_theme_rebuild_scheduled = true;
}

void ui_apply_pending_theme(void)
{
    if (s_theme_rebuild_pending) ui_request_theme_rebuild();
}

void ui_boot_ready(void)
{
    s_boot_ready = true;
    if (s_theme_rebuild_pending) ui_request_theme_rebuild();
}

static void ui_live_timer(lv_timer_t *timer)
{
    (void)timer;
    ui_mailbox_t events;
    portENTER_CRITICAL(&s_mailbox_lock);
    events = s_mailbox;
    s_mailbox.player_dirty = s_mailbox.ota_dirty = false;
    s_mailbox.wifi_dirty = s_mailbox.setup_dirty = false;
    portEXIT_CRITICAL(&s_mailbox_lock);
    if (events.wifi_dirty) {
        app_radio_network_changed(events.connected, ui_on_ota_status);
        ui_player_refresh_wifi();
        ui_player_refresh();
        ui_settings_network_changed();
    }
    if (events.setup_dirty) {
        switch (events.setup_event) {
        case WIFI_MGR_SETUP: ui_show_setup(WIFI_SETUP_AP_SSID, WIFI_SETUP_IP, events.ssid); break;
        case WIFI_MGR_CONNECTING: ui_show_setup_connecting(events.ssid); break;
        case WIFI_MGR_SETUP_CLOSED: ui_hide_setup(); break;
        default: break;
        }
    }
    if (events.player_dirty) { ui_player_refresh(); ui_stations_refresh_list(); }
    if (events.ota_dirty) { ui_player_refresh_ota_banner(); ui_info_refresh_ota(); }
    if (app_radio_tick(esp_timer_get_time())) {
        ui_player_refresh();
        ui_player_update_sleep_label();
        ui_player_update_sleep_countdown();
        ui_player_set_display_awake_locked(false);
    }
    ui_player_tick(++s_live_ticks);
    if (s_live_ticks % 25 == 0) {
        if (app_radio_state()->theme_mode == THEME_MODE_AUTO) ui_theme_apply_theme_choice(false);
        if (s_theme_rebuild_pending) ui_request_theme_rebuild();
    }
}

void ui_set_wifi_connected(bool connected)
{
    on_wifi_event(connected ? WIFI_MGR_CONNECTED : WIFI_MGR_DISCONNECTED, NULL);
}

esp_err_t ui_show_screen(ui_screen_t screen)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    esp_err_t result = ui_navigate_locked(screen);
    lvgl_port_unlock();
    return result;
}

esp_err_t ui_init(void)
{
    // 比 Wi-Fi 校时更早设置时区，热启动时不会先按 UTC 误判夜间主题。
    setenv("TZ", "CST-8", 1);
    tzset();
    app_radio_init();
    app_radio_state()->theme_dark = ui_theme_desired_dark();
    if (!lvgl_port_lock(2000)) return ESP_FAIL;
    ui_boot_build();
    lv_scr_load(ui_page_screen(UI_SCREEN_BOOT));
    build_pages();
    lv_timer_create(ui_live_timer, 40, NULL);
    ui_boot_start_timer();
    lv_refr_now(lv_disp_get_default());
    lvgl_port_unlock();
    ESP_ERROR_CHECK(board_backlight_fade_to(app_radio_state()->brightness, 420));
    player_set_cb(on_player_state);
    wifi_mgr_set_callback(on_wifi_event);
    ESP_LOGI(TAG, "界面就绪");
    return ESP_OK;
}

bool ui_responsive(void)
{
    if (!lvgl_port_lock(100)) return false;
    lvgl_port_unlock();
    return true;
}

esp_err_t ui_play_catalog_index(int index)
{
    const station_t *station = stations_catalog_get(index);
    if (!station) return ESP_ERR_INVALID_ARG;
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    app_radio_state()->filter.region = STATION_REGION_ALL;
    app_radio_state()->filter.cat = STATION_CAT_ALL;
    app_radio_state()->play_station = *station;
    app_radio_state()->play_source = STATION_SRC_CATALOG;
    app_radio_state()->has_station = true;
    preferences_save_last_station();
    player_play(&app_radio_state()->play_station);
    ui_player_refresh();
    ui_navigate_locked(UI_SCREEN_PLAYER);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_set_catalog_filter(int region, int category)
{
    if (region < 0 || region >= stations_region_count() ||
        category < 0 || category >= stations_cat_count()) return ESP_ERR_INVALID_ARG;
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    app_radio_state()->filter.region = region;
    app_radio_state()->filter.cat = category;
    app_radio_state()->tab = STATION_SRC_CATALOG;
    app_radio_state()->catalog_page = 0;
    preferences_save_tab();
    ui_stations_refresh_list();
    ui_navigate_locked(UI_SCREEN_STATIONS);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_step_station(int direction)
{
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    app_radio_step_station(direction < 0 ? -1 : 1);
    ui_player_refresh();
    ui_navigate_locked(UI_SCREEN_PLAYER);
    lvgl_port_unlock();
    return ESP_OK;
}
