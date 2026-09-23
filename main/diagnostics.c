#include "diagnostics.h"
#ifdef RADIO_UI_TEST
#include "ui_probe.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_mgr.h"
#include "player.h"
#include "stations.h"
#include "ui.h"
#include "wifi_mgr.h"

static const char *state_name(player_state_t state)
{
    switch (state) {
    case PLAYER_BUFFERING: return "buffering";
    case PLAYER_PLAYING: return "playing";
    case PLAYER_ERROR: return "error";
    default: return "stopped";
    }
}

static void print_help(void)
{
    puts("DIAG commands:");
    puts("  status | selftest | author | help | drawer <open|close> | donation <open|close>");
    puts("  display <on|off> | sleep <off|seconds>");
    puts("  theme <light|dark|auto> | accent <0-3|hue 0-359> | schedule <start-hour> <end-hour>");
    puts("  catalog [offset] [count] | regions | categories");
    puts("  play <catalog-index> | stop | next | prev | volume <0-100>");
    puts("  ota <status|check|install>");
    puts("  screen <player|stations|settings|appearance|theme-color|filter|wifi|wifi-pass|about|ota|diagnostics|touch>");
    puts("  filter <region-index> <category-index>");
}

static void print_status(void)
{
    player_metrics_t metrics = { 0 };
    station_t station = { 0 };
    char ssid[33] = { 0 };
    player_metrics(&metrics);
    player_current_station(&station);
    wifi_mgr_current_ssid(ssid, sizeof(ssid));
    printf("STATUS version=%s author=BI8SYN ota=%s wifi=%s ssid=\"%s\" rssi=%d player=%s volume=%d "
           "level=%d buffer=%d rate=%d bitrate=%d net_kbps=%d reconnects=%d underruns=%d "
           "station=\"%s\" catalog=%d favorites=%d theme=%s/%s accent=%d hue=%d "
           "dark_hours=%02d-%02d settings_y=%d\n",
           ota_mgr_running_version(), ota_mgr_service_configured() ? "configured" : "unconfigured",
           wifi_mgr_is_connected() ? "connected" : "offline", ssid, wifi_mgr_rssi(),
           state_name(player_state()), player_volume(), metrics.level, metrics.buffer,
           metrics.sample_rate, metrics.bitrate_kbps, metrics.network_kbps,
           metrics.reconnects, metrics.underruns, station.name,
           stations_catalog_count(), stations_fav_count(), ui_theme_mode_name(),
           ui_theme_is_dark() ? "dark" : "light", ui_theme_accent(), ui_theme_custom_hue(),
           ui_dark_start_hour(), ui_dark_end_hour(), ui_settings_scroll_y());
    printf("SPECTRUM");
    for (int i = 0; i < PLAYER_SPECTRUM_BANDS; i++) printf(" %d", metrics.bands[i]);
    putchar('\n');
}

static void run_selftest(void)
{
    unsigned internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    unsigned largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    unsigned minimum_internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    unsigned psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    bool semver = ota_mgr_version_compare("1.2.0-test", "1.1.0") > 0 &&
                  ota_mgr_version_compare("1.1.0-beta.2", "1.1.0") < 0 &&
                  ota_mgr_version_compare("v1.1.0", "1.1.0+build.7") == 0;
    bool ui_ok = ui_responsive();
    // 播放任务会按 HTTP 接收阶段短时持有约 20 KB 内部缓冲。总余量与最大连续块
    // 同时达标才算健康，避免旧的单一 48 KB 门槛把正常拉流误报成失败。
    bool memory_ok = internal >= 32 * 1024 && largest_internal >= 8 * 1024 &&
                     minimum_internal >= 8 * 1024;
    bool pass = BOARD_LCD_INVERT_COLOR && stations_catalog_count() >= 500 && semver && ui_ok &&
                board_codec() != NULL && memory_ok && psram >= 1024 * 1024;
    printf("SELFTEST %s invert=%s catalog=%d codec=%s semver=%s ui=%s "
           "internal=%u largest=%u minimum=%u psram=%u\n",
           pass ? "PASS" : "FAIL", BOARD_LCD_INVERT_COLOR ? "INVON" : "INVOFF",
           stations_catalog_count(), board_codec() ? "ok" : "missing",
           semver ? "ok" : "fail", ui_ok ? "ok" : "blocked", internal,
           largest_internal, minimum_internal, psram);
}

static const char *ota_state_name(ota_mgr_state_t state)
{
    switch (state) {
    case OTA_MGR_CHECKING: return "checking";
    case OTA_MGR_UPDATE_AVAILABLE: return "available";
    case OTA_MGR_DOWNLOADING: return "downloading";
    case OTA_MGR_UP_TO_DATE: return "up-to-date";
    case OTA_MGR_READY_TO_RESTART: return "restarting";
    case OTA_MGR_UNAVAILABLE: return "unavailable";
    case OTA_MGR_ERROR: return "error";
    default: return "idle";
    }
}

static int number_or(const char *text, int fallback)
{
    if (!text || !text[0]) return fallback;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    return end && *end == '\0' ? (int)value : fallback;
}

static void handle_line(char *line)
{
#ifdef RADIO_UI_TEST
    if (ui_probe_command(line)) return;
#endif
    char *save = NULL;
    char *cmd = strtok_r(line, " \t\r\n", &save);
    if (!cmd) return;
    if (!strcmp(cmd, "help")) { print_help(); return; }
    if (!strcmp(cmd, "author")) { puts("AUTHOR BI8SYN"); return; }
    if (!strcmp(cmd, "status")) { print_status(); return; }
    if (!strcmp(cmd, "selftest")) { run_selftest(); return; }
    if (!strcmp(cmd, "theme")) {
        const char *mode = strtok_r(NULL, " \t\r\n", &save);
        int value = mode && !strcmp(mode, "light") ? 0 :
                    mode && !strcmp(mode, "dark") ? 1 :
                    mode && !strcmp(mode, "auto") ? 2 : -1;
        printf("RESULT theme %s\n", ui_set_theme_mode(value) == ESP_OK ? "ok" : "invalid");
        return;
    }
    if (!strcmp(cmd, "accent")) {
        const char *choice = strtok_r(NULL, " \t\r\n", &save);
        esp_err_t err;
        if (choice && !strcmp(choice, "hue")) {
            int hue = number_or(strtok_r(NULL, " \t\r\n", &save), -1);
            err = ui_set_theme_custom_hue(hue);
        } else {
            err = ui_set_theme_accent(number_or(choice, -1));
        }
        printf("RESULT accent %s\n", err == ESP_OK ? "ok" : "invalid");
        return;
    }
    if (!strcmp(cmd, "schedule")) {
        int start = number_or(strtok_r(NULL, " \t\r\n", &save), -1);
        int end = number_or(strtok_r(NULL, " \t\r\n", &save), -1);
        printf("RESULT schedule %s\n",
               ui_set_dark_schedule(start, end) == ESP_OK ? "ok" : "invalid");
        return;
    }
    if (!strcmp(cmd, "ota")) {
        const char *action = strtok_r(NULL, " \t\r\n", &save);
        if (action && !strcmp(action, "check")) {
            printf("RESULT ota check %s\n", ota_mgr_check(NULL, NULL) == ESP_OK ? "ok" : "failed");
            return;
        }
        if (action && !strcmp(action, "install")) {
            printf("RESULT ota install %s\n", ota_mgr_start(NULL, NULL) == ESP_OK ? "ok" : "failed");
            return;
        }
        if (action && strcmp(action, "status")) { puts("RESULT ota invalid"); return; }
        ota_mgr_status_t status = { 0 };
        ota_mgr_status(&status);
        printf("OTA state=%s progress=%d running=%s available=%s message=\"%s\" notes=\"%s\"\n",
               ota_state_name(status.state), status.progress, ota_mgr_running_version(),
               status.available_version, status.message, status.release_notes);
        return;
    }
    if (!strcmp(cmd, "regions")) {
        for (int i = 0; i < stations_region_count(); ++i)
            printf("REGION %d %s %d\n", i, stations_region_label(i), stations_region_station_count(i));
        return;
    }
    if (!strcmp(cmd, "categories")) {
        for (int i = 0; i < stations_cat_count(); ++i)
            printf("CATEGORY %d %s %d\n", i, stations_cat_label(i), stations_cat_station_count(i));
        return;
    }
    if (!strcmp(cmd, "catalog")) {
        int offset = number_or(strtok_r(NULL, " \t\r\n", &save), 0);
        int count = number_or(strtok_r(NULL, " \t\r\n", &save), 10);
        if (offset < 0) offset = 0;
        if (count < 1) count = 1;
        if (count > 20) count = 20;
        int end = offset + count;
        if (end > stations_catalog_count()) end = stations_catalog_count();
        for (int i = offset; i < end; ++i) {
            const station_t *station = stations_catalog_get(i);
            printf("STATION %d \"%s\" \"%s\"\n", i, station->name, station->meta);
        }
        return;
    }
    if (!strcmp(cmd, "play")) {
        int index = number_or(strtok_r(NULL, " \t\r\n", &save), -1);
        printf("RESULT play %d %s\n", index, ui_play_catalog_index(index) == ESP_OK ? "ok" : "invalid");
        return;
    }
    if (!strcmp(cmd, "stop")) { player_stop(); puts("RESULT stop ok"); return; }
    if (!strcmp(cmd, "next")) { ui_step_station(1); puts("RESULT next ok"); return; }
    if (!strcmp(cmd, "prev")) { ui_step_station(-1); puts("RESULT prev ok"); return; }
    if (!strcmp(cmd, "volume")) {
        int value = number_or(strtok_r(NULL, " \t\r\n", &save), -1);
        if (value < 0 || value > 100) { puts("RESULT volume invalid"); return; }
        player_set_volume(value);
        player_save_volume();
        printf("RESULT volume %d ok\n", value);
        return;
    }
    if (!strcmp(cmd, "filter")) {
        int region = number_or(strtok_r(NULL, " \t\r\n", &save), -1);
        int category = number_or(strtok_r(NULL, " \t\r\n", &save), -1);
        printf("RESULT filter %s\n",
               ui_set_catalog_filter(region, category) == ESP_OK ? "ok" : "invalid");
        return;
    }
    if (!strcmp(cmd, "drawer")) {
        const char *state = strtok_r(NULL, " \t\r\n", &save);
        if (!state || (strcmp(state, "open") && strcmp(state, "close"))) {
            puts("RESULT drawer invalid");
            return;
        }
        printf("RESULT drawer %s\n",
               ui_set_drawer_visible(!strcmp(state, "open")) == ESP_OK ? "ok" : "failed");
        return;
    }
    if (!strcmp(cmd, "donation")) {
        const char *state = strtok_r(NULL, " \t\r\n", &save);
        if (!state || (strcmp(state, "open") && strcmp(state, "close"))) {
            puts("RESULT donation invalid");
            return;
        }
        printf("RESULT donation %s\n",
               ui_set_donation_visible(!strcmp(state, "open")) == ESP_OK ? "ok" : "failed");
        return;
    }
    if (!strcmp(cmd, "display")) {
        const char *state = strtok_r(NULL, " \t\r\n", &save);
        if (!state || (strcmp(state, "on") && strcmp(state, "off"))) {
            puts("RESULT display invalid");
            return;
        }
        printf("RESULT display %s\n",
               ui_set_display_awake(!strcmp(state, "on")) == ESP_OK ? "ok" : "failed");
        return;
    }
    if (!strcmp(cmd, "sleep")) {
        const char *value = strtok_r(NULL, " \t\r\n", &save);
        int seconds = value && !strcmp(value, "off") ? 0 : number_or(value, -1);
        printf("RESULT sleep %s\n",
               ui_set_sleep_timer_seconds(seconds) == ESP_OK ? "ok" : "invalid");
        return;
    }
    if (!strcmp(cmd, "screen")) {
        const char *name = strtok_r(NULL, " \t\r\n", &save);
        if (name && !strcmp(name, "appearance")) {
            printf("RESULT screen %s\n",
                   ui_show_appearance_settings() == ESP_OK ? "ok" : "failed");
            return;
        }
        if (name && !strcmp(name, "theme-color")) {
            printf("RESULT screen %s\n",
                   ui_show_theme_picker() == ESP_OK ? "ok" : "failed");
            return;
        }
        ui_screen_t screen;
        if (name && !strcmp(name, "player")) screen = UI_SCREEN_PLAYER;
        else if (name && !strcmp(name, "stations")) screen = UI_SCREEN_STATIONS;
        else if (name && !strcmp(name, "settings")) screen = UI_SCREEN_SETTINGS;
        else if (name && !strcmp(name, "filter")) screen = UI_SCREEN_FILTER;
        else if (name && !strcmp(name, "wifi")) screen = UI_SCREEN_WIFI;
        else if (name && !strcmp(name, "wifi-pass")) screen = UI_SCREEN_WIFI_PASSWORD;
        else if (name && !strcmp(name, "about")) screen = UI_SCREEN_ABOUT;
        else if (name && !strcmp(name, "ota")) screen = UI_SCREEN_OTA;
        else if (name && !strcmp(name, "diagnostics")) screen = UI_SCREEN_DIAGNOSTICS;
        else if (name && !strcmp(name, "touch")) screen = UI_SCREEN_TOUCH_TEST;
        else { puts("RESULT screen invalid"); return; }
        printf("RESULT screen %s\n", ui_show_screen(screen) == ESP_OK ? "ok" : "failed");
        return;
    }
    printf("RESULT unknown \"%s\"; type help\n", cmd);
}

static void diagnostics_task(void *arg)
{
    (void)arg;
    char line[128] = { 0 };
    size_t used = 0;
    puts("DIAG READY author=BI8SYN; type help");
    while (true) {
        uint8_t ch;
        int got = usb_serial_jtag_read_bytes(&ch, 1, portMAX_DELAY);
        if (got <= 0) continue;
        if (ch == '\r' || ch == '\n') {
            if (used > 0) {
                line[used] = '\0';
                handle_line(line);
                used = 0;
            }
        } else if ((ch == 8 || ch == 127) && used > 0) {
            --used;
        } else if (ch >= 32 && used + 1 < sizeof(line)) {
            line[used++] = (char)ch;
        }
    }
}

esp_err_t diagnostics_init(void)
{
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 2048,
        .rx_buffer_size = 256,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    usb_serial_jtag_vfs_use_driver();
#ifdef RADIO_UI_TEST
    // 截图会在诊断任务里运行 LVGL 绘制递归，仅测试构建需要额外栈空间。
    const uint32_t stack_size = 12288;
#else
    const uint32_t stack_size = 4096;
#endif
    return xTaskCreate(diagnostics_task, "diag", stack_size, NULL, 2, NULL) == pdPASS
           ? ESP_OK : ESP_ERR_NO_MEM;
}
