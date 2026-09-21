// 网络音频播放：HTTP 拉流 → PSRAM 环形缓冲 → MP3 解码（libhelix）→ I2S → ES8311
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "stations.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAYER_STOPPED = 0,
    PLAYER_BUFFERING,
    PLAYER_PLAYING,
    PLAYER_ERROR,
} player_state_t;

// 状态变化回调。msg 仅在 PLAYER_ERROR 时有意义（给用户看的中文原因）。
// 在播放器任务上下文里调用，UI 侧要自己加锁。
typedef void (*player_cb_t)(player_state_t state, const char *msg);

#define PLAYER_SPECTRUM_BANDS 9

typedef struct {
    int level;          // 0-100，来自实际 PCM RMS 响度的快攻慢释结果
    int buffer;         // 0-100，网络环形缓冲占用率
    int sample_rate;
    int bitrate_kbps;
    int channels;
    int network_kbps;   // 最近接收窗口的有效下载速率
    int reconnects;     // 当前播放会话的重连次数
    int underruns;      // 当前播放会话的缓冲耗尽次数
    int bands[PLAYER_SPECTRUM_BANDS]; // 低频到高频，真实 PCM FFT 能量（0-100）
} player_metrics_t;

esp_err_t player_init(void);
void player_set_cb(player_cb_t cb);

// 开始播放。会先停掉当前播放。
esp_err_t player_play(const station_t *st);
void player_stop(void);

player_state_t player_state(void);
const char *player_error_msg(void);
void player_metrics(player_metrics_t *out);
void player_current_station(station_t *out);

// 音量 0–100。拖动时只调硬件，松手后再保存，避免磨损 NVS。
void player_set_volume(int vol);
void player_save_volume(void);
int player_volume(void);

#ifdef __cplusplus
}
#endif
