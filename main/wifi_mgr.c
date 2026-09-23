#include "wifi_mgr.h"

#include <stdlib.h>   // strtol（url_decode 用）
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "nvs.h"

static wifi_mgr_callback_t s_callback;
void wifi_mgr_set_callback(wifi_mgr_callback_t callback) { s_callback = callback; }
static void notify(wifi_mgr_event_t event, const char *ssid)
{
    if (s_callback) s_callback(event, ssid);
}

static const char *TAG = "wifi";

#define NVS_NS        "radio"
#define KEY_SSID      "wifi_ssid"
#define KEY_PASS      "wifi_pass"
#define MAX_RETRY     6
#define SCAN_MAX      20

static bool s_connected;
static bool s_portal_up;          // 配网热点 + 网页是否已启动
static int s_retry;
static httpd_handle_t s_httpd;
static TimerHandle_t s_reconnect_timer;
static esp_netif_t *s_ap_netif;
static char s_saved_ssid[33];     // 只用于给用户看「连不上哪个网络」

static wifi_ap_record_t s_scan[SCAN_MAX];
static uint16_t s_scan_n;
static void creds_save(const char *ssid, const char *pass);

bool wifi_mgr_is_connected(void) { return s_connected; }

int wifi_mgr_rssi(void)
{
    wifi_ap_record_t ap;
    return s_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : -127;
}

void wifi_mgr_current_ssid(char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    out[0] = '\0';
    wifi_ap_record_t ap;
    if (s_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        strlcpy(out, (const char *)ap.ssid, out_len);
    } else if (s_saved_ssid[0]) {
        strlcpy(out, s_saved_ssid, out_len);
    }
}

esp_err_t wifi_mgr_scan(wifi_mgr_ap_t *items, size_t capacity, size_t *count)
{
    if (!items || !count || capacity == 0) return ESP_ERR_INVALID_ARG;
    wifi_scan_config_t cfg = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) { *count = 0; return err; }

    uint16_t n = capacity > WIFI_MGR_SCAN_MAX ? WIFI_MGR_SCAN_MAX : (uint16_t)capacity;
    wifi_ap_record_t records[WIFI_MGR_SCAN_MAX];
    err = esp_wifi_scan_get_ap_records(&n, records);
    if (err != ESP_OK) { *count = 0; return err; }

    size_t used = 0;
    for (int i = 0; i < n && used < capacity; i++) {
        const char *ssid = (const char *)records[i].ssid;
        if (!ssid[0]) continue;
        bool duplicate = false;
        for (size_t j = 0; j < used; j++) {
            if (strcmp(items[j].ssid, ssid) == 0) { duplicate = true; break; }
        }
        if (duplicate) continue;
        strlcpy(items[used].ssid, ssid, sizeof(items[used].ssid));
        items[used].rssi = records[i].rssi;
        items[used].secured = records[i].authmode != WIFI_AUTH_OPEN;
        used++;
    }
    *count = used;
    return ESP_OK;
}

esp_err_t wifi_mgr_connect(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || (password && strlen(password) > 63)) {
        return ESP_ERR_INVALID_ARG;
    }
    creds_save(ssid, password ? password : "");
    strlcpy(s_saved_ssid, ssid, sizeof(s_saved_ssid));
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password ? password : "", sizeof(cfg.sta.password));
    s_retry = 0;
    if (s_reconnect_timer) xTimerStop(s_reconnect_timer, 0);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) return err;
    esp_wifi_disconnect();
    return esp_wifi_connect();
}

// ------------------------------------------------------------------ 凭据存取
static bool creds_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sl = ssid_len, pl = pass_len;
    bool ok = (nvs_get_str(h, KEY_SSID, ssid, &sl) == ESP_OK) && ssid[0];
    if (ok && nvs_get_str(h, KEY_PASS, pass, &pl) != ESP_OK) pass[0] = '\0';
    nvs_close(h);
    return ok;
}

static void creds_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, KEY_SSID, ssid);
    nvs_set_str(h, KEY_PASS, pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
}

esp_err_t wifi_mgr_reset(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return ESP_FAIL;
    nvs_erase_key(h, KEY_SSID);
    nvs_erase_key(h, KEY_PASS);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

// ------------------------------------------------------------------ 扫描
// 只在启动热点之前扫一次：此时还没有手机连上来，扫描造成的信道跳变不会打断配网页。
static void scan_networks(void)
{
    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
        ESP_LOGW(TAG, "扫描失败");
        s_scan_n = 0;
        return;
    }
    s_scan_n = SCAN_MAX;
    if (esp_wifi_scan_get_ap_records(&s_scan_n, s_scan) != ESP_OK) s_scan_n = 0;
    ESP_LOGI(TAG, "扫到 %d 个 2.4GHz 网络：", s_scan_n);
    for (int i = 0; i < s_scan_n; i++) {
        ESP_LOGI(TAG, "  [%d] %s  (%d dBm)", i, (const char *)s_scan[i].ssid, s_scan[i].rssi);
    }
}

// ------------------------------------------------------------------ 配网页
static void html_send_escaped(httpd_req_t *req, const char *text)
{
    for (const char *p = text; p && *p; p++) {
        switch (*p) {
        case '&': httpd_resp_sendstr_chunk(req, "&amp;"); break;
        case '<': httpd_resp_sendstr_chunk(req, "&lt;"); break;
        case '>': httpd_resp_sendstr_chunk(req, "&gt;"); break;
        case '"': httpd_resp_sendstr_chunk(req, "&quot;"); break;
        case '\'': httpd_resp_sendstr_chunk(req, "&#39;"); break;
        default: {
            char ch[2] = { *p, 0 };
            httpd_resp_sendstr_chunk(req, ch);
            break;
        }
        }
    }
}

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>收音机联网设置</title><style>"
        "*{box-sizing:border-box}body{font-family:system-ui,-apple-system,sans-serif;max-width:440px;"
        "margin:0 auto;padding:32px 20px;background:#f8f8f5;color:#17171c}"
        "main{background:#eeeef2;padding:24px;border-radius:20px}"
        "h1{font-size:24px;margin:0 0 8px}p{color:#696970;font-size:14px;margin:0 0 20px}"
        "label{display:block;font-size:13px;color:#696970;margin:16px 0 6px}"
        "select,input{width:100%;box-sizing:border-box;padding:11px;font-size:16px;"
        "border:1px solid #d9d9df;border-radius:12px;background:#fff;color:#17171c}"
        "button{width:100%;margin-top:24px;padding:14px;font-size:16px;font-weight:650;"
        "background:#635bff;color:#fff;border:0;border-radius:12px}"
        ".hint{font-size:12px;color:#696970;margin-top:8px}"
        "</style></head><body><main><h1>收音机联网设置</h1>");

    // 说明为什么会看到这个页面
    if (s_saved_ssid[0]) {
        httpd_resp_sendstr_chunk(req, "<p>连不上已保存的网络「");
        html_send_escaped(req, s_saved_ssid);
        httpd_resp_sendstr_chunk(req, "」，请重新选择。</p>");
    } else {
        httpd_resp_sendstr_chunk(req, "<p>请选择要连接的 Wi-Fi。</p>");
    }

    httpd_resp_sendstr_chunk(req, "<form method=POST action=/save><label>Wi-Fi 名称</label>");

    if (s_scan_n > 0) {
        httpd_resp_sendstr_chunk(req, "<select id=sel onchange=\"pick()\">");
        char buf[128];
        for (int i = 0; i < s_scan_n; i++) {
            const char *ssid = (const char *)s_scan[i].ssid;
            if (!ssid[0]) continue;
            int bars = s_scan[i].rssi > -55 ? 3 : (s_scan[i].rssi > -70 ? 2 : 1);
            const char *sig = bars == 3 ? "●●●" : (bars == 2 ? "●●○" : "●○○");
            httpd_resp_sendstr_chunk(req, "<option value=\"");
            html_send_escaped(req, ssid);
            httpd_resp_sendstr_chunk(req, "\">");
            html_send_escaped(req, ssid);
            snprintf(buf, sizeof(buf), " &nbsp;%s</option>", sig);
            httpd_resp_sendstr_chunk(req, buf);
        }
        httpd_resp_sendstr_chunk(req,
            "<option value=\"\">— 手动输入 —</option></select>"
            "<div class=hint>只列出 2.4GHz 网络。找不到你的网络？多半是 5GHz，"
            "请在路由器上开 2.4GHz 频段。</div>");
    } else {
        httpd_resp_sendstr_chunk(req,
            "<div class=hint>没扫到任何 2.4GHz 网络，请手动输入。"
            "ESP32 不支持 5GHz。</div>");
    }

    httpd_resp_sendstr_chunk(req,
        "<input name=s id=ssid required maxlength=31 placeholder=\"Wi-Fi 名称\">"
        "<label>密码</label>"
        "<input name=p type=password maxlength=63 placeholder=\"没有密码就留空\">"
        "<button type=submit>保存并连接</button></form>"
        "<script>function pick(){var v=document.getElementById('sel').value;"
        "if(v)document.getElementById('ssid').value=v;}"
        "window.onload=pick;</script>"
        "</main></body></html>");

    return httpd_resp_sendstr_chunk(req, NULL);
}

// application/x-www-form-urlencoded 解码（只有两个短字段，够用）
static void url_decode(char *dst, const char *src, size_t dstlen)
{
    size_t di = 0;
    for (size_t i = 0; src[i] && di + 1 < dstlen; i++) {
        if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            char hex[3] = { src[i + 1], src[i + 2], 0 };
            dst[di++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[di++] = ' ';
        } else {
            dst[di++] = src[i];
        }
    }
    dst[di] = '\0';
}

static esp_err_t save_post(httpd_req_t *req)
{
    char buf[256] = { 0 };
    int total = req->content_len < (int)sizeof(buf) - 1 ? req->content_len : (int)sizeof(buf) - 1;
    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, total - got);
        if (r <= 0) return ESP_FAIL;
        got += r;
    }
    buf[got] = '\0';

    char raw_s[96] = { 0 }, raw_p[128] = { 0 };
    httpd_query_key_value(buf, "s", raw_s, sizeof(raw_s));
    httpd_query_key_value(buf, "p", raw_p, sizeof(raw_p));

    char ssid[33] = { 0 }, pass[65] = { 0 };
    url_decode(ssid, raw_s, sizeof(ssid));
    url_decode(pass, raw_p, sizeof(pass));

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (!ssid[0]) {
        return httpd_resp_sendstr(req,
            "<meta charset=utf-8><body style=\"font-family:system-ui;padding:24px\">"
            "<p>Wi-Fi 名称不能为空，请返回重填。");
    }

    ESP_LOGI(TAG, "收到配网：SSID=%s", ssid);
    creds_save(ssid, pass);
    strlcpy(s_saved_ssid, ssid, sizeof(s_saved_ssid));

    httpd_resp_sendstr(req,
        "<!doctype html><meta charset=utf-8>"
        "<body style=\"font-family:system-ui;padding:24px;color:#141414\">"
        "<h2>已保存</h2><p>设备正在连接，请看屏幕。<br>"
        "连上后这个热点会自动关闭；若屏幕提示失败，重新连回热点再试一次。</p>");

    notify(WIFI_MGR_CONNECTING, ssid);

    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    s_retry = 0;
    esp_wifi_disconnect();
    esp_wifi_connect();
    return ESP_OK;
}

static void portal_start_httpd(void)
{
    if (s_httpd) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6144;   // 页面是分块拼的，给足栈
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "配网页启动失败");
        return;
    }
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_post };
    httpd_register_uri_handler(s_httpd, &root);
    httpd_register_uri_handler(s_httpd, &save);
}

// 进入配网模式：先扫描（此时还没客户端），再开热点和网页。幂等。
static void enter_setup_mode(void)
{
    if (s_portal_up) {
        notify(WIFI_MGR_SETUP, s_saved_ssid[0] ? s_saved_ssid : NULL);
        return;
    }

    scan_networks();

    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, WIFI_SETUP_AP_SSID, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(WIFI_SETUP_AP_SSID);
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));

    portal_start_httpd();
    s_portal_up = true;

    ESP_LOGI(TAG, "配网模式：热点 %s，网页 http://%s", WIFI_SETUP_AP_SSID, WIFI_SETUP_IP);
    notify(WIFI_MGR_SETUP, s_saved_ssid[0] ? s_saved_ssid : NULL);
}

static void exit_setup_mode(void)
{
    if (!s_portal_up) return;
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = NULL; }
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_portal_up = false;
    ESP_LOGI(TAG, "已连上，关闭配网热点");
    notify(WIFI_MGR_SETUP_CLOSED, NULL);
}

static void reconnect_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (!s_connected && !s_portal_up) esp_wifi_connect();
}

esp_err_t wifi_mgr_start_setup(void)
{
    enter_setup_mode();
    return ESP_OK;
}

// ------------------------------------------------------------------ 事件
static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        notify(WIFI_MGR_DISCONNECTED, NULL);

        // 配网热点已经开着的时候不再自动重连：STA 重连会让 AP 跟着跳信道，
        // 正在填密码的手机会被踢下线。
        if (s_portal_up) {
            notify(WIFI_MGR_SETUP, s_saved_ssid[0] ? s_saved_ssid : NULL);
            return;
        }

        if (s_retry < MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "连接失败，重试 %d/%d", s_retry, MAX_RETRY);
            xTimerStart(s_reconnect_timer, 0);
        } else {
            ESP_LOGW(TAG, "重试 %d 次都失败，转入配网模式", MAX_RETRY);
            enter_setup_mode();
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "已连接，IP " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry = 0;
        s_connected = true;
        xTimerStop(s_reconnect_timer, 0);
        notify(WIFI_MGR_CONNECTED, NULL);
        exit_setup_mode();
    }
}

// ------------------------------------------------------------------ 初始化
esp_err_t wifi_mgr_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // 北京时间 + 国内优先 NTP；联网后后台同步，UI 在未同步前显示占位符。
    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_init();

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));
    s_reconnect_timer = xTimerCreate("wifi_retry", pdMS_TO_TICKS(2000), pdFALSE, NULL,
                                     reconnect_timer_cb);
    if (!s_reconnect_timer) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        on_wifi_event, NULL, NULL));

    char pass[65] = { 0 };
    bool have = creds_load(s_saved_ssid, sizeof(s_saved_ssid), pass, sizeof(pass));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    if (have) {
        ESP_LOGI(TAG, "使用已保存的 Wi-Fi：%s", s_saved_ssid);
        wifi_config_t cfg = { 0 };
        strlcpy((char *)cfg.sta.ssid, s_saved_ssid, sizeof(cfg.sta.ssid));
        strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    }
    ESP_ERROR_CHECK(esp_wifi_start());   // STA_START 事件里会发起连接
    // 收音机是持续小流量下行设备且没有电池。关闭 modem sleep，避免 DTIM 唤醒
    // 给实时音频带来周期性吞吐凹陷；信号格数正常时也可能因此频繁耗尽缓冲。
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "连续音频模式：Wi-Fi 省电已关闭");

    if (!have) {
        ESP_LOGI(TAG, "未配置 Wi-Fi，进入配网模式");
        enter_setup_mode();
    }
    return ESP_OK;
}
