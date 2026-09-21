#include "player.h"

#include <math.h>
#include <string.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mp3dec.h"
#include "nvs.h"

static const char *TAG = "player";

#define RING_SIZE      (128 * 1024)   // 64kbps 约 8KB/s → 16 秒余量
#define PREBUFFER      (24 * 1024)    // 起播前先攒 3 秒，避免开头断续
#define REBUFFER       (16 * 1024)    // 弱网耗尽后攒约 2 秒再恢复，减少碎片化卡顿
#define MP3_INBUF      (8 * 1024)
#define PCM_MAX_SAMP   1152           // MP3 单帧最大采样点数
#define NVS_NS         "radio"
#define NVS_KEY_VOL    "vol"
#define NET_RETRIES    3
#define SPECTRUM_FFT_N 256

// ------------------------------------------------------- PSRAM 环形缓冲（单产单消）
typedef struct {
    uint8_t *buf;
    size_t size;
    volatile size_t head;   // 写
    volatile size_t tail;   // 读
    SemaphoreHandle_t mtx;
    SemaphoreHandle_t data_sem;   // 有新数据
    SemaphoreHandle_t space_sem;  // 有空位
    volatile bool abort;
} ring_t;

static ring_t s_ring;

static bool ring_init(ring_t *r, size_t size)
{
    r->buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!r->buf) return false;
    r->size = size;
    r->head = r->tail = 0;
    r->abort = false;
    r->mtx = xSemaphoreCreateMutex();
    r->data_sem = xSemaphoreCreateBinary();
    r->space_sem = xSemaphoreCreateBinary();
    return r->mtx && r->data_sem && r->space_sem;
}

static size_t ring_used(ring_t *r)
{
    size_t h = r->head, t = r->tail;
    return (h >= t) ? (h - t) : (r->size - t + h);
}

static size_t ring_free(ring_t *r)
{
    return r->size - ring_used(r) - 1;
}

static void ring_reset(ring_t *r)
{
    xSemaphoreTake(r->mtx, portMAX_DELAY);
    r->head = r->tail = 0;
    r->abort = false;
    xSemaphoreGive(r->mtx);
}

// 写入，缓冲满时等待。返回实际写入字节数，abort 时返回 0。
static size_t ring_write(ring_t *r, const uint8_t *src, size_t len)
{
    size_t written = 0;
    while (written < len && !r->abort) {
        size_t space = ring_free(r);
        if (space == 0) {
            xSemaphoreTake(r->space_sem, pdMS_TO_TICKS(100));
            continue;
        }
        size_t n = len - written;
        if (n > space) n = space;

        xSemaphoreTake(r->mtx, portMAX_DELAY);
        size_t first = r->size - r->head;
        if (first > n) first = n;
        memcpy(r->buf + r->head, src + written, first);
        if (n > first) memcpy(r->buf, src + written + first, n - first);
        r->head = (r->head + n) % r->size;
        xSemaphoreGive(r->mtx);

        written += n;
        xSemaphoreGive(r->data_sem);
    }
    return written;
}

// 读取，最多 len 字节。没数据时等待，abort 或超时返回已读到的量。
static size_t ring_read(ring_t *r, uint8_t *dst, size_t len)
{
    size_t got = 0;
    while (got < len && !r->abort) {
        size_t used = ring_used(r);
        if (used == 0) {
            if (xSemaphoreTake(r->data_sem, pdMS_TO_TICKS(200)) != pdTRUE && got > 0) break;
            continue;
        }
        size_t n = len - got;
        if (n > used) n = used;

        xSemaphoreTake(r->mtx, portMAX_DELAY);
        size_t first = r->size - r->tail;
        if (first > n) first = n;
        memcpy(dst + got, r->buf + r->tail, first);
        if (n > first) memcpy(dst + got + first, r->buf, n - first);
        r->tail = (r->tail + n) % r->size;
        xSemaphoreGive(r->mtx);

        got += n;
        xSemaphoreGive(r->space_sem);
    }
    return got;
}

// ---------------------------------------------------------------- 播放器状态
static player_state_t s_state = PLAYER_STOPPED;
static char s_err[48];
static station_t s_cur;
static int s_volume = 60;
static volatile int s_level;
static volatile int s_sample_rate;
static volatile int s_bitrate_kbps;
static volatile int s_channels;
static volatile int s_network_kbps;
static volatile int s_reconnects;
static volatile int s_underruns;
static volatile int s_bands[PLAYER_SPECTRUM_BANDS];

static TaskHandle_t s_net_task;
static TaskHandle_t s_dec_task;
static TaskHandle_t s_ctrl_task;
static QueueHandle_t s_cmd_queue;
static volatile bool s_running;        // 本次播放会话是否有效
static volatile bool s_net_done;       // 网络侧已结束（正常或异常）
static player_cb_t s_cb;

static HMP3Decoder s_mp3;
static uint8_t *s_inbuf;
static int16_t *s_pcm;
static int16_t *s_pcm_stereo;
static float *s_fft_re;
static float *s_fft_im;
static float *s_fft_window;
static bool s_codec_open;
static int s_open_rate, s_open_ch;

typedef enum { PLAYER_CMD_STOP = 0, PLAYER_CMD_PLAY } player_cmd_type_t;
typedef struct {
    player_cmd_type_t type;
    station_t station;
} player_cmd_t;

player_state_t player_state(void) { return s_state; }
const char *player_error_msg(void) { return s_err; }
int player_volume(void) { return s_volume; }

void player_metrics(player_metrics_t *out)
{
    if (!out) return;
    out->level = s_level;
    out->buffer = (int)(ring_used(&s_ring) * 100 / (s_ring.size ? s_ring.size : 1));
    out->sample_rate = s_sample_rate;
    out->bitrate_kbps = s_bitrate_kbps;
    out->channels = s_channels;
    out->network_kbps = s_network_kbps;
    out->reconnects = s_reconnects;
    out->underruns = s_underruns;
    for (int i = 0; i < PLAYER_SPECTRUM_BANDS; i++) out->bands[i] = s_bands[i];
}

void player_current_station(station_t *out)
{
    if (out) *out = s_cur;
}

void player_set_cb(player_cb_t cb) { s_cb = cb; }

static void set_state(player_state_t st, const char *msg)
{
    s_state = st;
    if (st != PLAYER_PLAYING) {
        s_level = 0;
        for (int i = 0; i < PLAYER_SPECTRUM_BANDS; i++) s_bands[i] = 0;
    }
    if (msg) snprintf(s_err, sizeof(s_err), "%s", msg);
    else s_err[0] = '\0';
    if (s_cb) s_cb(st, s_err);
}

// 取一帧双声道 PCM 的 256 个等距采样点，做 Hann 窗 + radix-2 FFT。
// 可视化只需要稳定的对数频段能量，不追求测量仪器级频率精度。
static void update_spectrum(const int16_t *pcm, int stereo_frames)
{
    if (!pcm || stereo_frames < SPECTRUM_FFT_N || !s_fft_re || !s_fft_im || !s_fft_window) return;

    for (int i = 0; i < SPECTRUM_FFT_N; i++) {
        int frame = (int)((int64_t)i * stereo_frames / SPECTRUM_FFT_N);
        int sample = ((int)pcm[frame * 2] + (int)pcm[frame * 2 + 1]) / 2;
        s_fft_re[i] = (float)sample * s_fft_window[i];
        s_fft_im[i] = 0.0f;
    }

    for (unsigned i = 1, j = 0; i < SPECTRUM_FFT_N; i++) {
        unsigned bit = SPECTRUM_FFT_N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = s_fft_re[i]; s_fft_re[i] = s_fft_re[j]; s_fft_re[j] = tr;
            float ti = s_fft_im[i]; s_fft_im[i] = s_fft_im[j]; s_fft_im[j] = ti;
        }
    }

    for (int len = 2; len <= SPECTRUM_FFT_N; len <<= 1) {
        float angle = -2.0f * (float)M_PI / (float)len;
        float wlen_r = cosf(angle), wlen_i = sinf(angle);
        for (int base = 0; base < SPECTRUM_FFT_N; base += len) {
            float wr = 1.0f, wi = 0.0f;
            for (int j = 0; j < len / 2; j++) {
                int even = base + j, odd = even + len / 2;
                float vr = s_fft_re[odd] * wr - s_fft_im[odd] * wi;
                float vi = s_fft_re[odd] * wi + s_fft_im[odd] * wr;
                float ur = s_fft_re[even], ui = s_fft_im[even];
                s_fft_re[even] = ur + vr; s_fft_im[even] = ui + vi;
                s_fft_re[odd] = ur - vr; s_fft_im[odd] = ui - vi;
                float next_wr = wr * wlen_r - wi * wlen_i;
                wi = wr * wlen_i + wi * wlen_r;
                wr = next_wr;
            }
        }
    }

    // 对数近似分带：中心视觉柱为最低频，向外逐步升高。
    static const uint8_t edges[PLAYER_SPECTRUM_BANDS + 1] = { 1, 2, 4, 7, 12, 20, 32, 50, 78, 128 };
    for (int band = 0; band < PLAYER_SPECTRUM_BANDS; band++) {
        float power = 0.0f;
        int count = 0;
        for (int bin = edges[band]; bin < edges[band + 1]; bin++) {
            float re = s_fft_re[bin], im = s_fft_im[bin];
            power += re * re + im * im;
            count++;
        }
        float magnitude = count ? sqrtf(power / count) / (SPECTRUM_FFT_N * 16384.0f) : 0.0f;
        float db = 20.0f * log10f(magnitude + 0.000001f);
        int target = (int)((db + 58.0f) * 100.0f / 48.0f);
        if (target < 0) target = 0;
        if (target > 100) target = 100;
        int current = s_bands[band];
        s_bands[band] = target > current ? (current + target * 3) / 4
                                           : (current * 7 + target) / 8;
    }
}

void player_set_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    s_volume = vol;
    if (board_codec()) {
        esp_codec_dev_set_out_vol(board_codec(), vol);
    }
}

void player_save_volume(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, NVS_KEY_VOL, s_volume);
        nvs_commit(h);
        nvs_close(h);
    }
}

// ------------------------------------------------------------------ 网络任务
static void net_task(void *arg)
{
    (void)arg;
    uint8_t *chunk = heap_caps_malloc(4096, MALLOC_CAP_DEFAULT);
    if (!chunk) {
        set_state(PLAYER_ERROR, "播放失败：内存不足");
        goto done;
    }

    char final_error[48] = "播放失败：网络中断";
    int64_t rate_started = esp_timer_get_time();
    size_t rate_bytes = 0;
    for (int attempt = 0; attempt < NET_RETRIES && s_running; attempt++) {
        esp_http_client_config_t cfg = {
            .url = s_cur.url,
            .timeout_ms = 5000,
            .user_agent = "es3c28p-radio/1.0",
            .buffer_size = 4096,
            .keep_alive_enable = true,
        };
        esp_http_client_handle_t cli = esp_http_client_init(&cfg);
        if (!cli) {
            snprintf(final_error, sizeof(final_error), "播放失败：初始化出错");
        } else if (esp_http_client_open(cli, 0) != ESP_OK) {
            snprintf(final_error, sizeof(final_error), "播放失败：连接超时");
        } else {
            esp_http_client_fetch_headers(cli);
            int status = esp_http_client_get_status_code(cli);
            if (status != 200) {
                snprintf(final_error, sizeof(final_error), "播放失败：服务器返回 %d", status);
            } else {
                ESP_LOGI(TAG, "已连接：%s（第 %d 次）", s_cur.url, attempt + 1);
                if (attempt > 0) set_state(PLAYER_PLAYING, NULL);
                while (s_running) {
                    int r = esp_http_client_read(cli, (char *)chunk, 4096);
                    if (r == -ESP_ERR_HTTP_EAGAIN) continue;
                    if (r <= 0) break;
                    if (ring_write(&s_ring, chunk, r) == 0) break;
                    rate_bytes += (size_t)r;
                    int64_t now = esp_timer_get_time();
                    if (now - rate_started >= 5000000) {
                        s_network_kbps = (int)((rate_bytes * 8 * 1000000LL) /
                                               (now - rate_started) / 1000);
                        ESP_LOGI(TAG, "网络速率 %d kbps，缓冲 %d%%",
                                 s_network_kbps,
                                 (int)(ring_used(&s_ring) * 100 / s_ring.size));
                        rate_bytes = 0;
                        rate_started = now;
                    }
                }
            }
        }
        if (cli) {
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
        }

        if (!s_running) break;
        if (attempt + 1 < NET_RETRIES) {
            s_reconnects++;
            ESP_LOGW(TAG, "流中断，准备重连 %d/%d", attempt + 2, NET_RETRIES);
            set_state(PLAYER_BUFFERING, NULL);
            vTaskDelay(pdMS_TO_TICKS(500 * (attempt + 1)));
        } else {
            set_state(PLAYER_ERROR, final_error);
        }
    }
    free(chunk);

done:
    s_net_done = true;
    s_ring.abort = true;             // 唤醒解码任务，让它收尾
    xSemaphoreGive(s_ring.data_sem);
    s_net_task = NULL;
    vTaskDelete(NULL);
}

// ------------------------------------------------------------------ 解码任务
static bool ensure_codec_open(int rate, int ch)
{
    if (s_codec_open && s_open_rate == rate && s_open_ch == ch) return true;
    if (s_codec_open) {
        esp_codec_dev_close(board_codec());
        s_codec_open = false;
    }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,            // ES8311 走双声道 I2S；单声道 MP3 会被复制成两路
        .channel_mask = 0,
        .sample_rate = rate,
        .mclk_multiple = 256,
    };
    if (esp_codec_dev_open(board_codec(), &fs) != 0) {
        ESP_LOGE(TAG, "codec 打开失败 rate=%d", rate);
        return false;
    }
    esp_codec_dev_set_out_vol(board_codec(), s_volume);
    s_codec_open = true;
    s_open_rate = rate;
    s_open_ch = ch;
    ESP_LOGI(TAG, "codec 已打开：%d Hz，源声道 %d", rate, ch);
    return true;
}

static void dec_task(void *arg)
{
    (void)arg;

    // 起播前先攒够数据，否则开头会断续
    while (s_running && !s_net_done && ring_used(&s_ring) < PREBUFFER) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!s_running || s_state == PLAYER_ERROR) goto cleanup;

    set_state(PLAYER_PLAYING, NULL);

    int bytes_left = 0;
    uint8_t *read_ptr = s_inbuf;

    while (s_running) {
        if (!s_net_done && ring_used(&s_ring) == 0) {
            s_underruns++;
            set_state(PLAYER_BUFFERING, NULL);
            ESP_LOGW(TAG, "缓冲耗尽，第 %d 次重新蓄水", s_underruns);
            while (s_running && !s_net_done && ring_used(&s_ring) < REBUFFER) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            if (!s_running) break;
            if (ring_used(&s_ring) >= REBUFFER) set_state(PLAYER_PLAYING, NULL);
        }
        // 补数据：把剩余数据挪到头部再填满
        if (bytes_left < 2 * 1024) {
            if (bytes_left > 0 && read_ptr != s_inbuf) {
                memmove(s_inbuf, read_ptr, bytes_left);
            }
            read_ptr = s_inbuf;
            size_t want = MP3_INBUF - bytes_left;
            size_t got = ring_read(&s_ring, s_inbuf + bytes_left, want);
            bytes_left += got;
            if (got == 0) {
                if (s_net_done) break;      // 流结束
                if (!s_running) break;
                continue;
            }
        }

        int offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (offset < 0) {
            bytes_left = 0;                 // 整块都没同步字，丢掉重来
            read_ptr = s_inbuf;
            continue;
        }
        read_ptr += offset;
        bytes_left -= offset;

        int err = MP3Decode(s_mp3, &read_ptr, &bytes_left, s_pcm, 0);
        if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
            continue;                       // 数据不够，下轮补
        }
        if (err != ERR_MP3_NONE) {
            if (bytes_left > 0) { read_ptr++; bytes_left--; }   // 跳一字节重新找同步
            continue;
        }

        MP3FrameInfo fi;
        MP3GetLastFrameInfo(s_mp3, &fi);
        if (fi.samprate <= 0 || fi.outputSamps <= 0) continue;
        s_sample_rate = fi.samprate;
        s_bitrate_kbps = fi.bitrate > 0 ? fi.bitrate / 1000 : 0;
        s_channels = fi.nChans;

        if (!ensure_codec_open(fi.samprate, fi.nChans)) {
            set_state(PLAYER_ERROR, "播放失败：音频设备出错");
            break;
        }

        int16_t *out = s_pcm;
        int out_bytes;
        if (fi.nChans == 1) {
            // 单声道复制成双声道，I2S 配置保持不变
            for (int i = 0; i < fi.outputSamps; i++) {
                s_pcm_stereo[2 * i] = s_pcm[i];
                s_pcm_stereo[2 * i + 1] = s_pcm[i];
            }
            out = s_pcm_stereo;
            out_bytes = fi.outputSamps * 2 * sizeof(int16_t);
        } else {
            out_bytes = fi.outputSamps * sizeof(int16_t);
        }

        uint64_t square_sum = 0;
        int level_samples = 0;
        int samples = out_bytes / (int)sizeof(int16_t);
        for (int i = 0; i < samples; i += 4) {
            // 合并左右声道并隔帧取样。RMS 比瞬时峰值更接近听感响度，
            // 不会被单个尖峰拉满，适合作为中心视觉锚点。
            int32_t mono = ((int32_t)out[i] + (int32_t)out[i + 1]) / 2;
            square_sum += (uint64_t)((int64_t)mono * mono);
            ++level_samples;
        }
        float rms = level_samples ? sqrtf((float)square_sum / (float)level_samples) / 32768.0f : 0.0f;
        float rms_db = 20.0f * log10f(rms + 0.000001f);
        int instant = (int)((rms_db + 50.0f) * 100.0f / 42.0f);
        if (instant < 0) instant = 0;
        if (instant > 100) instant = 100;
        // 快攻慢释：声音出现时立即跟上，回落则保留少量惯性。
        s_level = instant > s_level ? (s_level + instant * 2) / 3
                                    : (s_level * 7 + instant) / 8;
        update_spectrum(out, samples / 2);

        if (esp_codec_dev_write(board_codec(), out, out_bytes) != 0) {
            set_state(PLAYER_ERROR, "播放失败：音频设备出错");
            break;
        }
    }

cleanup:
    if (s_codec_open) {
        esp_codec_dev_close(board_codec());
        s_codec_open = false;
    }
    // 正常停止（用户按停）不改状态；异常已在别处置为 ERROR
    if (s_running && s_state == PLAYER_PLAYING) {
        set_state(PLAYER_ERROR, "播放失败：网络中断");
    }
    s_dec_task = NULL;
    vTaskDelete(NULL);
}

// -------------------------------------------------------------------- 播放控制
// 只在控制任务中调用：确保旧网络/解码任务完全退出后，才允许复用共享缓冲。
static void stop_session(void)
{
    s_running = false;
    s_ring.abort = true;
    xSemaphoreGive(s_ring.data_sem);
    xSemaphoreGive(s_ring.space_sem);

    // HTTP 读超时为 5 秒；这里留足时间，禁止旧任务和新会话并存。
    for (int i = 0; i < 300 && (s_net_task || s_dec_task); i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_net_task || s_dec_task) {
        ESP_LOGE(TAG, "旧播放任务未按时退出，拒绝启动新会话");
    }
}

static void start_session(const station_t *st)
{
    s_cur = *st;
    s_level = s_sample_rate = s_bitrate_kbps = s_channels = 0;
    s_network_kbps = s_reconnects = s_underruns = 0;
    for (int i = 0; i < PLAYER_SPECTRUM_BANDS; i++) s_bands[i] = 0;
    ring_reset(&s_ring);
    s_running = true;
    s_net_done = false;
    set_state(PLAYER_BUFFERING, NULL);

    if (xTaskCreate(net_task, "aud_net", 5120, NULL, 5, &s_net_task) != pdPASS) {
        s_running = false;
        set_state(PLAYER_ERROR, "播放失败：任务创建失败");
        return;
    }
    // 解码是 CPU 大头，优先级高一点、栈给足
    if (xTaskCreate(dec_task, "aud_dec", 6144, NULL, 6, &s_dec_task) != pdPASS) {
        s_running = false;
        s_ring.abort = true;
        xSemaphoreGive(s_ring.data_sem);
        xSemaphoreGive(s_ring.space_sem);
        set_state(PLAYER_ERROR, "播放失败：任务创建失败");
        return;
    }
}

static void control_task(void *arg)
{
    (void)arg;
    player_cmd_t cmd;
    while (true) {
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;
        stop_session();

        // 用户连续点了多个台时，只执行停止期间收到的最后一个命令。
        player_cmd_t newer;
        while (xQueueReceive(s_cmd_queue, &newer, 0) == pdTRUE) cmd = newer;

        if (s_net_task || s_dec_task) {
            set_state(PLAYER_ERROR, "播放失败：旧任务未退出");
            continue;
        }
        if (cmd.type == PLAYER_CMD_PLAY) start_session(&cmd.station);
    }
}

// -------------------------------------------------------------------- 对外
void player_stop(void)
{
    if (!s_cmd_queue) return;
    player_cmd_t cmd = { .type = PLAYER_CMD_STOP };
    s_running = false;
    s_ring.abort = true;
    xSemaphoreGive(s_ring.data_sem);
    xSemaphoreGive(s_ring.space_sem);
    set_state(PLAYER_STOPPED, NULL);
    xQueueOverwrite(s_cmd_queue, &cmd);
}

esp_err_t player_play(const station_t *st)
{
    if (!st) return ESP_ERR_INVALID_ARG;
    if (!board_codec() || !s_cmd_queue) return ESP_ERR_INVALID_STATE;

    player_cmd_t cmd = { .type = PLAYER_CMD_PLAY, .station = *st };
    set_state(PLAYER_BUFFERING, NULL);
    return xQueueOverwrite(s_cmd_queue, &cmd) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t player_init(void)
{
    if (!ring_init(&s_ring, RING_SIZE)) {
        ESP_LOGE(TAG, "环形缓冲分配失败");
        return ESP_ERR_NO_MEM;
    }

    s_mp3 = MP3InitDecoder();
    if (!s_mp3) {
        ESP_LOGE(TAG, "MP3 解码器初始化失败");
        return ESP_ERR_NO_MEM;
    }

    s_inbuf = heap_caps_malloc(MP3_INBUF, MALLOC_CAP_DEFAULT);
    s_pcm = heap_caps_malloc(PCM_MAX_SAMP * 2 * sizeof(int16_t), MALLOC_CAP_DEFAULT);
    s_pcm_stereo = heap_caps_malloc(PCM_MAX_SAMP * 2 * sizeof(int16_t), MALLOC_CAP_DEFAULT);
    s_fft_re = heap_caps_malloc(SPECTRUM_FFT_N * sizeof(float), MALLOC_CAP_SPIRAM);
    s_fft_im = heap_caps_malloc(SPECTRUM_FFT_N * sizeof(float), MALLOC_CAP_SPIRAM);
    s_fft_window = heap_caps_malloc(SPECTRUM_FFT_N * sizeof(float), MALLOC_CAP_SPIRAM);
    if (!s_inbuf || !s_pcm || !s_pcm_stereo || !s_fft_re || !s_fft_im || !s_fft_window) {
        ESP_LOGE(TAG, "解码缓冲分配失败");
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < SPECTRUM_FFT_N; i++) {
        s_fft_window[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (SPECTRUM_FFT_N - 1));
    }

    s_cmd_queue = xQueueCreate(1, sizeof(player_cmd_t));
    if (!s_cmd_queue || xTaskCreate(control_task, "aud_ctrl", 4096, NULL, 7, &s_ctrl_task) != pdPASS) {
        ESP_LOGE(TAG, "播放控制任务初始化失败");
        return ESP_ERR_NO_MEM;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        int32_t v = 60;
        if (nvs_get_i32(h, NVS_KEY_VOL, &v) == ESP_OK) {
            s_volume = (v < 0) ? 0 : (v > 100 ? 100 : v);
        }
        nvs_close(h);
    }

    ESP_LOGI(TAG, "播放器就绪（环形缓冲 %d KB，音量 %d）", RING_SIZE / 1024, s_volume);
    return ESP_OK;
}
