#include "ota_mgr.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

#ifndef RADIO_OTA_URL
#define RADIO_OTA_URL ""
#endif

#define MANIFEST_MAX_BYTES 1536
#define DOWNLOAD_BUFFER_BYTES 4096

// P-256 公钥随固件发布；签名私钥只保存在 OTA 服务器上。
static const char OTA_PUBLIC_KEY[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEIJ6j7V4X4yzG/LOT3jXCihORl1zm\n"
    "QbQw3McR65RaeM8vcznUwPOEPLptDKYX05fzz8VuN7hWSrgqlwaqOPMTPA==\n"
    "-----END PUBLIC KEY-----\n";

typedef struct {
    char version[32];
    char url[256];
    char sha256[65];
    char notes[192];
    char signature[128];
} ota_manifest_t;

typedef struct {
    char data[MANIFEST_MAX_BYTES + 1];
    size_t length;
    bool overflow;
} manifest_buffer_t;

static const char *TAG = "ota";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ota_mgr_status_t s_status;
static bool s_busy;
static ota_mgr_cb_t s_callback;
static void *s_callback_arg;

static void publish(ota_mgr_state_t state, int progress, const char *version,
                    const char *message, const char *notes)
{
    ota_mgr_cb_t callback;
    void *callback_arg;
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.progress = progress;
    snprintf(s_status.available_version, sizeof(s_status.available_version), "%s", version ? version : "");
    snprintf(s_status.message, sizeof(s_status.message), "%s", message ? message : "");
    snprintf(s_status.release_notes, sizeof(s_status.release_notes), "%s", notes ? notes : "");
    callback = s_callback;
    callback_arg = s_callback_arg;
    portEXIT_CRITICAL(&s_lock);
    if (callback) callback(&s_status, callback_arg);
}

static const char *skip_v(const char *version)
{
    return version && (*version == 'v' || *version == 'V') ? version + 1 : version;
}

static long next_number(const char **cursor)
{
    const char *p = *cursor;
    long value = 0;
    while (*p && isdigit((unsigned char)*p)) {
        value = value * 10 + (*p - '0');
        ++p;
    }
    if (*p == '.') ++p;
    *cursor = p;
    return value;
}

static const char *prerelease_part(const char *version)
{
    const char *dash = version ? strchr(version, '-') : NULL;
    return dash ? dash + 1 : NULL;
}

static int compare_identifier(const char *a, size_t an, const char *b, size_t bn)
{
    bool a_number = an > 0;
    bool b_number = bn > 0;
    for (size_t i = 0; i < an; ++i) a_number = a_number && isdigit((unsigned char)a[i]);
    for (size_t i = 0; i < bn; ++i) b_number = b_number && isdigit((unsigned char)b[i]);
    if (a_number && b_number) {
        while (an > 1 && *a == '0') { ++a; --an; }
        while (bn > 1 && *b == '0') { ++b; --bn; }
        if (an != bn) return an > bn ? 1 : -1;
    } else if (a_number != b_number) {
        return a_number ? -1 : 1;
    }
    size_t common = an < bn ? an : bn;
    int result = strncmp(a, b, common);
    if (result != 0) return result > 0 ? 1 : -1;
    return an == bn ? 0 : (an > bn ? 1 : -1);
}

int ota_mgr_version_compare(const char *left, const char *right)
{
    const char *a = skip_v(left ? left : "0");
    const char *b = skip_v(right ? right : "0");
    for (int field = 0; field < 3; ++field) {
        long av = next_number(&a);
        long bv = next_number(&b);
        if (av != bv) return av > bv ? 1 : -1;
    }
    const char *ap = prerelease_part(skip_v(left));
    const char *bp = prerelease_part(skip_v(right));
    if (!ap && !bp) return 0;
    if (!ap) return 1;
    if (!bp) return -1;
    while (*ap || *bp) {
        if (!*ap) return -1;
        if (!*bp) return 1;
        const char *ae = ap;
        const char *be = bp;
        while (*ae && *ae != '.' && *ae != '+') ++ae;
        while (*be && *be != '.' && *be != '+') ++be;
        int result = compare_identifier(ap, (size_t)(ae - ap), bp, (size_t)(be - bp));
        if (result) return result;
        ap = *ae == '.' ? ae + 1 : ae;
        bp = *be == '.' ? be + 1 : be;
        if (*ap == '+') ap = "";
        if (*bp == '+') bp = "";
    }
    return 0;
}

static esp_err_t manifest_event(esp_http_client_event_t *event)
{
    manifest_buffer_t *buffer = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || !buffer || event->data_len <= 0) return ESP_OK;
    size_t available = MANIFEST_MAX_BYTES - buffer->length;
    if ((size_t)event->data_len > available) {
        buffer->overflow = true;
        return ESP_FAIL;
    }
    memcpy(buffer->data + buffer->length, event->data, event->data_len);
    buffer->length += event->data_len;
    buffer->data[buffer->length] = '\0';
    return ESP_OK;
}

static bool is_lower_hex_sha256(const char *text)
{
    if (!text || strlen(text) != 64) return false;
    for (int i = 0; i < 64; ++i) {
        if (!isdigit((unsigned char)text[i]) && (text[i] < 'a' || text[i] > 'f')) return false;
    }
    return true;
}

static bool verify_manifest_signature(const ota_manifest_t *manifest)
{
    char canonical[768];
    int length = snprintf(canonical, sizeof(canonical), "%s\n%s\n%s\n%s\n",
                          manifest->version, manifest->url, manifest->sha256, manifest->notes);
    if (length <= 0 || length >= (int)sizeof(canonical)) return false;
    unsigned char digest[32];
    if (mbedtls_sha256((const unsigned char *)canonical, (size_t)length, digest, 0) != 0) return false;

    unsigned char signature[80];
    size_t signature_length = 0;
    if (mbedtls_base64_decode(signature, sizeof(signature), &signature_length,
                              (const unsigned char *)manifest->signature,
                              strlen(manifest->signature)) != 0) return false;
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    int result = mbedtls_pk_parse_public_key(&key, (const unsigned char *)OTA_PUBLIC_KEY,
                                             sizeof(OTA_PUBLIC_KEY));
    if (result == 0) {
        result = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                                   signature, signature_length);
    }
    mbedtls_pk_free(&key);
    return result == 0;
}

static esp_err_t fetch_manifest(ota_manifest_t *manifest)
{
    manifest_buffer_t buffer = { 0 };
    esp_http_client_config_t config = {
        .url = RADIO_OTA_URL,
        .timeout_ms = 12000,
        .keep_alive_enable = true,
        .buffer_size = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = manifest_event,
        .user_data = &buffer,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status < 200 || status >= 300 || buffer.overflow || buffer.length == 0) {
        ESP_LOGE(TAG, "manifest request failed: err=%s status=%d bytes=%u overflow=%d",
                 esp_err_to_name(err), status, (unsigned)buffer.length, buffer.overflow);
        return err == ESP_OK ? ESP_FAIL : err;
    }

    cJSON *root = cJSON_ParseWithLength(buffer.data, buffer.length);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "url");
    const cJSON *sha256 = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    const cJSON *notes = cJSON_GetObjectItemCaseSensitive(root, "notes");
    const cJSON *signature = cJSON_GetObjectItemCaseSensitive(root, "signature");
    bool valid = cJSON_IsString(version) && version->valuestring[0] &&
                 cJSON_IsString(url) && (!strncmp(url->valuestring, "http://", 7) ||
                                        !strncmp(url->valuestring, "https://", 8)) &&
                 cJSON_IsString(sha256) && is_lower_hex_sha256(sha256->valuestring) &&
                 cJSON_IsString(notes) && cJSON_IsString(signature);
    if (valid) {
        snprintf(manifest->version, sizeof(manifest->version), "%s", version->valuestring);
        snprintf(manifest->url, sizeof(manifest->url), "%s", url->valuestring);
        snprintf(manifest->sha256, sizeof(manifest->sha256), "%s", sha256->valuestring);
        snprintf(manifest->notes, sizeof(manifest->notes), "%s", notes->valuestring);
        snprintf(manifest->signature, sizeof(manifest->signature), "%s", signature->valuestring);
        valid = verify_manifest_signature(manifest);
    }
    cJSON_Delete(root);
    if (!valid) {
        ESP_LOGE(TAG, "manifest signature or fields invalid");
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

static bool decode_sha256(const char *hex, unsigned char out[32])
{
    for (int i = 0; i < 32; ++i) {
        char pair[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
        char *end = NULL;
        long value = strtol(pair, &end, 16);
        if (!end || *end) return false;
        out[i] = (unsigned char)value;
    }
    return true;
}

static esp_err_t download_and_install(const ota_manifest_t *manifest)
{
    // 擦除 OTA 分区必须先于联网；否则长时间擦除会让已建立的下载连接超时。
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (!partition) return ESP_ERR_NOT_FOUND;
    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) return err;

    esp_http_client_config_t config = {
        .url = manifest->url,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
        .buffer_size = DOWNLOAD_BUFFER_BYTES,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        esp_ota_abort(ota_handle);
        return ESP_ERR_NO_MEM;
    }
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) goto cleanup_ota;
    int64_t total = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        err = ESP_ERR_HTTP_FETCH_HEADER;
        goto cleanup_ota;
    }

    unsigned char *chunk = malloc(DOWNLOAD_BUFFER_BYTES);
    if (!chunk) {
        err = ESP_ERR_NO_MEM;
        goto cleanup_ota;
    }
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    int64_t received = 0;
    int last_progress = -1;
    while (true) {
        int read = esp_http_client_read(client, (char *)chunk, DOWNLOAD_BUFFER_BYTES);
        if (read > 0) {
            mbedtls_sha256_update(&sha, chunk, (size_t)read);
            err = esp_ota_write(ota_handle, chunk, (size_t)read);
            if (err != ESP_OK) break;
            received += read;
            int progress = total > 0 ? (int)(received * 100 / total) : 0;
            if (progress >= last_progress + 2 || progress == 100) {
                last_progress = progress;
                publish(OTA_MGR_DOWNLOADING, progress, manifest->version,
                        "正在下载并校验固件…", manifest->notes);
            }
        } else if (read == 0) {
            err = ESP_OK;
            break;
        } else if (read != -ESP_ERR_HTTP_EAGAIN) {
            err = ESP_FAIL;
            break;
        }
    }
    unsigned char actual[32];
    unsigned char expected[32];
    mbedtls_sha256_finish(&sha, actual);
    mbedtls_sha256_free(&sha);
    free(chunk);

    if (err != ESP_OK || !esp_http_client_is_complete_data_received(client) ||
        !decode_sha256(manifest->sha256, expected) || memcmp(actual, expected, 32) != 0) {
        ESP_LOGE(TAG, "firmware download/hash failed: err=%s bytes=%lld",
                 esp_err_to_name(err), (long long)received);
        err = ESP_ERR_INVALID_CRC;
        goto cleanup_ota;
    }
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) goto cleanup_client;
    ota_handle = 0;

    esp_app_desc_t candidate = { 0 };
    err = esp_ota_get_partition_description(partition, &candidate);
    if (err != ESP_OK || strcmp(candidate.project_name, esp_app_get_description()->project_name) != 0 ||
        strcmp(candidate.version, manifest->version) != 0 ||
        ota_mgr_version_compare(candidate.version, ota_mgr_running_version()) <= 0) {
        ESP_LOGE(TAG, "image identity mismatch: candidate=%s manifest=%s", candidate.version, manifest->version);
        err = ESP_ERR_INVALID_VERSION;
        goto cleanup_client;
    }
    err = esp_ota_set_boot_partition(partition);
    goto cleanup_client;

cleanup_ota:
    esp_ota_abort(ota_handle);
cleanup_client:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

static void ota_task(void *arg)
{
    bool install = (bool)(intptr_t)arg;
    ota_manifest_t manifest = { 0 };
    publish(OTA_MGR_CHECKING, 0, NULL, "正在验证更新信息…", NULL);
    esp_err_t err = fetch_manifest(&manifest);
    if (err != ESP_OK) {
        publish(OTA_MGR_ERROR, 0, NULL, "无法验证更新服务，请稍后重试", NULL);
        goto done;
    }
    int comparison = ota_mgr_version_compare(manifest.version, ota_mgr_running_version());
    ESP_LOGI(TAG, "signed version check: running=%s server=%s result=%d",
             ota_mgr_running_version(), manifest.version, comparison);
    if (comparison <= 0) {
        publish(OTA_MGR_UP_TO_DATE, 100, manifest.version, "当前已是最新版本", manifest.notes);
        goto done;
    }
    if (!install) {
        publish(OTA_MGR_UPDATE_AVAILABLE, 0, manifest.version, "发现可用更新", manifest.notes);
        goto done;
    }

    publish(OTA_MGR_DOWNLOADING, 0, manifest.version, "正在下载并校验固件…", manifest.notes);
    err = download_and_install(&manifest);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA install failed: %s", esp_err_to_name(err));
        publish(OTA_MGR_ERROR, 0, manifest.version, "更新校验失败，原固件未受影响", manifest.notes);
        goto done;
    }
    publish(OTA_MGR_READY_TO_RESTART, 100, manifest.version, "更新完成，即将重新启动…", manifest.notes);
    vTaskDelay(pdMS_TO_TICKS(1400));
    esp_restart();

done:
    portENTER_CRITICAL(&s_lock);
    s_busy = false;
    portEXIT_CRITICAL(&s_lock);
    vTaskDelete(NULL);
}

esp_err_t ota_mgr_confirm_running(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "firmware startup verified; confirming OTA image");
        return esp_ota_mark_app_valid_cancel_rollback();
    }
    return ESP_OK;
}

const char *ota_mgr_running_version(void) { return esp_app_get_description()->version; }
bool ota_mgr_service_configured(void) { return RADIO_OTA_URL[0] != '\0'; }

bool ota_mgr_busy(void)
{
    portENTER_CRITICAL(&s_lock);
    bool busy = s_busy;
    portEXIT_CRITICAL(&s_lock);
    return busy;
}

void ota_mgr_status(ota_mgr_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t start_job(bool install, ota_mgr_cb_t callback, void *user_data)
{
    if (!ota_mgr_service_configured()) {
        publish(OTA_MGR_UNAVAILABLE, 0, NULL, "更新服务尚未配置", NULL);
        return ESP_ERR_NOT_SUPPORTED;
    }
    portENTER_CRITICAL(&s_lock);
    if (s_busy) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_busy = true;
    if (callback) {
        s_callback = callback;
        s_callback_arg = user_data;
    }
    portEXIT_CRITICAL(&s_lock);
    if (xTaskCreate(ota_task, install ? "ota_install" : "ota_check", 10240,
                    (void *)(intptr_t)install, 5, NULL) != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_busy = false;
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ota_mgr_check(ota_mgr_cb_t callback, void *user_data) { return start_job(false, callback, user_data); }
esp_err_t ota_mgr_start(ota_mgr_cb_t callback, void *user_data) { return start_job(true, callback, user_data); }
