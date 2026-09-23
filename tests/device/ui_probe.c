// 仅 RADIO_UI_TEST=ON 的开发构建链接此文件；正式构建没有触摸注入或截图接口。
#include "ui_probe.h"
#include "ui/ui_internal.h"
#include "mbedtls/base64.h"

static lv_indev_t *s_input;
static int s_step = -1;
static lv_point_t s_from, s_to;
static void read_touch(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    if (s_step < 0) { data->state = LV_INDEV_STATE_RELEASED; return; }
    int step = s_step > 6 ? 6 : s_step;
    data->point.x = s_from.x + (s_to.x - s_from.x) * step / 6;
    data->point.y = s_from.y + (s_to.y - s_from.y) * step / 6;
    data->state = s_step < 7 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (++s_step > 7) s_step = -1;
}

static void snapshot(void)
{
#if LV_USE_SNAPSHOT
    lv_img_dsc_t image = {0};
    // 使用 PSRAM，避免测试截图挤占音频/网络所需的内部 RAM。
    size_t size = 320 * 240 * sizeof(lv_color_t);
    void *buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer || !lvgl_port_lock(3000)) {
        free(buffer); puts("UIFRAME ERROR allocation-or-lock"); return;
    }
    lv_obj_update_layout(lv_scr_act());
    lv_res_t result = lv_snapshot_take_to_buf(lv_scr_act(), LV_IMG_CF_TRUE_COLOR, &image, buffer, size);
    lvgl_port_unlock();
    if (result != LV_RES_OK) { free(buffer); puts("UIFRAME ERROR render"); return; }
    printf("UIFRAME BEGIN %u %u %u\n", image.header.w, image.header.h, (unsigned)image.data_size);
    for (unsigned offset = 0; offset < image.data_size; offset += 480) {
        unsigned bytes = image.data_size - offset;
        if (bytes > 480) bytes = 480;
        unsigned char encoded[641];
        size_t written = 0;
        mbedtls_base64_encode(encoded, sizeof(encoded), &written, (uint8_t *)buffer + offset, bytes);
        printf("UIFRAME DATA %u %.*s\n", offset, (int)written, encoded);
    }
    puts("UIFRAME END");
    free(buffer);
#else
    puts("UIFRAME ERROR enable-CONFIG_LV_USE_SNAPSHOT");
#endif
}

bool ui_probe_command(const char *command)
{
    if (strncmp(command, "uitest ", 7)) return false;
    command += 7;
    if (!strcmp(command, "frame")) { snapshot(); return true; }
    if (!lvgl_port_lock(3000)) { puts("UITEST ERROR lock"); return true; }
    if (!strcmp(command, "state")) {
        int active = -1;
        for (int i = UI_SCREEN_PLAYER; i <= UI_SCREEN_BOOT; ++i) {
            if (lv_scr_act() == ui_page_screen(i)) active = i;
        }
        const app_radio_state_t *state = app_radio_state();
        // 诊断音频线程切流有延迟；用 UI 模型地址的哈希验证选台，不输出流地址。
        uint32_t station_id = 2166136261u;
        for (const char *p = state->play_station.url; *p; ++p)
            station_id = (station_id ^ (uint8_t)*p) * 16777619u;
        printf("UITEST STATE screen=%d awake=%d sleep=%d tab=%d region=%d category=%d page=%d autoplay=%d bright=%d time24=%d shuffle=%d station_id=%lu\n",
               active, state->screen_awake, state->sleep_minutes, state->tab,
               state->filter.region, state->filter.cat, state->catalog_page,
               state->autoplay, state->brightness, state->time_24h, state->shuffle, (unsigned long)station_id);
    } else {
        int x, y, end_x, end_y;
        int parsed = sscanf(command, "touch %d %d %d %d", &x, &y, &end_x, &end_y);
        if (parsed == 2) { end_x = x; end_y = y; }
        if ((parsed != 2 && parsed != 4) || x < 0 || x >= 320 || y < 0 || y >= 240 ||
            end_x < 0 || end_x >= 320 || end_y < 0 || end_y >= 240 || s_step >= 0) {
            puts("UITEST ERROR touch");
        } else {
            if (!s_input) {
                static lv_indev_drv_t driver;
                lv_indev_drv_init(&driver);
                driver.type = LV_INDEV_TYPE_POINTER;
                driver.read_cb = read_touch;
                s_input = lv_indev_drv_register(&driver);
            }
            s_from = (lv_point_t){x, y}; s_to = (lv_point_t){end_x, end_y}; s_step = 0;
            puts("UITEST TOUCH queued");
        }
    }
    lvgl_port_unlock();
    return true;
}
