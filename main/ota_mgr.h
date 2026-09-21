#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 兼容之前通过 OTA 写入的镜像：系统关键组件启动成功后确认当前固件。
esp_err_t ota_mgr_confirm_running(void);

typedef enum {
    OTA_MGR_IDLE = 0,
    OTA_MGR_CHECKING,
    OTA_MGR_UPDATE_AVAILABLE,
    OTA_MGR_DOWNLOADING,
    OTA_MGR_UP_TO_DATE,
    OTA_MGR_READY_TO_RESTART,
    OTA_MGR_UNAVAILABLE,
    OTA_MGR_ERROR,
} ota_mgr_state_t;

typedef struct {
    ota_mgr_state_t state;
    int progress;
    char available_version[32];
    char message[80];
    char release_notes[192];
} ota_mgr_status_t;

typedef void (*ota_mgr_cb_t)(const ota_mgr_status_t *status, void *user_data);

const char *ota_mgr_running_version(void);
bool ota_mgr_service_configured(void);
bool ota_mgr_busy(void);
void ota_mgr_status(ota_mgr_status_t *out);
int ota_mgr_version_compare(const char *left, const char *right);
esp_err_t ota_mgr_check(ota_mgr_cb_t callback, void *user_data);
esp_err_t ota_mgr_start(ota_mgr_cb_t callback, void *user_data);

#ifdef __cplusplus
}
#endif
