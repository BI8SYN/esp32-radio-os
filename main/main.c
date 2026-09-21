// ES3C28P 网络收音机固件 —— 第一版
//
// 启动顺序：NVS → 屏/触摸/LVGL → 界面 → 音频 → 播放器 → Wi-Fi
// 先把界面点亮再联网，开机就能看到东西，不用干等网络。

#include "board.h"
#include "diagnostics.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "ota_mgr.h"
#include "player.h"
#include "stations.h"
#include "ui.h"
#include "wifi_mgr.h"

static const char *TAG = "main";

static void log_mem(const char *stage)
{
    ESP_LOGI(TAG, "[%s] 内部 RAM %u KB / PSRAM %u KB 可用",
             stage,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    log_mem("启动");

    // 逃生口：按住 BOOT 键上电 → 清掉 Wi-Fi 凭据重新配网（换路由器时用）
    gpio_config_t boot_cfg = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pin_bit_mask = 1ULL << BOARD_BOOT_BTN,
    };
    gpio_config(&boot_cfg);
    if (gpio_get_level(BOARD_BOOT_BTN) == 0) {
        ESP_LOGW(TAG, "检测到 BOOT 键按下，清除已保存的 Wi-Fi");
        wifi_mgr_reset();
    }

    ESP_ERROR_CHECK(stations_init());
    ESP_ERROR_CHECK(board_display_init());
    ESP_ERROR_CHECK(player_init());
    ESP_ERROR_CHECK(ui_init());

    log_mem("界面就绪");

    // 音频初始化失败不致命：界面照常能用，只是不出声
    if (board_audio_init() != ESP_OK) {
        ESP_LOGE(TAG, "音频初始化失败，播放功能不可用");
    }

    ESP_ERROR_CHECK(wifi_mgr_init());
    ESP_ERROR_CHECK(diagnostics_init());
    ESP_ERROR_CHECK(ota_mgr_confirm_running());
    log_mem("全部就绪");

    float v = board_battery_voltage();
    if (v > 0) ESP_LOGI(TAG, "电池电压 %.2f V", v);

    // 心跳：每 30 秒报一次内存，方便观察长跑时有没有泄漏
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        log_mem("运行中");
    }
}
