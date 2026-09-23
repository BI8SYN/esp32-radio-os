// 只负责音频数据到画布的视觉映射；不改变播放器状态，不负责页面导航。
#include "ui_internal.h"
static const char *TAG = "ui_visual";

static lv_obj_t *visual_canvas;
static lv_color_t *visual_buffer;
static int s_visual_levels[PLAYER_SPECTRUM_BANDS];
lv_obj_t *ui_visual_create(lv_obj_t *par, int width, int height)
{
    size_t bytes = LV_CANVAS_BUF_SIZE_TRUE_COLOR(width, height);
    visual_buffer = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!visual_buffer) {
        ESP_LOGE(TAG, "音频视觉画布分配失败：%u bytes", (unsigned)bytes);
        return ui_widgets_mk_box(par, width, height);
    }
    visual_canvas = lv_canvas_create(par);
    lv_canvas_set_buffer(visual_canvas, visual_buffer, width, height,
                         LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED);
    lv_canvas_fill_bg(visual_canvas, LV_COLOR_CHROMA_KEY, LV_OPA_COVER);
    return visual_canvas;
}

void ui_visual_render(const int bands[PLAYER_SPECTRUM_BANDS], int level)
{
    lv_obj_t *canvas = visual_canvas;
    if (!canvas) return;
    const int width = 236;
    const int height = 44;
    lv_canvas_fill_bg(canvas, LV_COLOR_CHROMA_KEY, LV_OPA_COVER);

    const int bar_w = 6;
    const int gap = 5;
    const int max_h = 34;
    const int total_w = 17 * bar_w + 16 * gap;
    const int start_x = (width - total_w) / 2;
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.radius = 3;

    // 中心不是 FFT 的最低频 bin：最低频很容易接近零，会让视觉锚点塌陷。
    // 用 RMS 响度和低中频能量共同驱动中心，再让真实频谱只负责细节。
    int low_mid = (bands[0] * 3 + bands[1] * 2 + bands[2] * 2 + bands[3]) / 8;
    int energy = (level * 3 + low_mid * 2) / 5;
    bool active = energy > 0;
    if (!active) {
        for (int band = 0; band < PLAYER_SPECTRUM_BANDS; ++band) {
            if (bands[band] > 0) { active = true; break; }
        }
    }

    static const uint8_t envelope[PLAYER_SPECTRUM_BANDS] = {
        100, 91, 82, 73, 64, 55, 46, 37, 28,
    };
    int targets[PLAYER_SPECTRUM_BANDS] = {0};
    targets[0] = active ? 28 + energy * 72 / 100 : 0;
    for (int distance = 1; distance < PLAYER_SPECTRUM_BANDS; ++distance) {
        int band = distance - 1;
        int spectral = bands[band];
        if (distance == PLAYER_SPECTRUM_BANDS - 1) {
            spectral = (bands[7] + bands[8]) / 2;
        }
        // 82% 稳定轮廓 + 18% 真实频段起伏。固定空间包络确保视线
        // 从中心自然向外衰减，同时仍能看出音乐频谱的变化。
        int shaped = (targets[0] * 82 + spectral * 18) / 100;
        targets[distance] = shaped * envelope[distance] / 100;
        int max_allowed = targets[distance - 1] > 2 ? targets[distance - 1] - 2 : 0;
        if (targets[distance] > max_allowed) targets[distance] = max_allowed;
    }

    for (int distance = 0; distance < PLAYER_SPECTRUM_BANDS; ++distance) {
        int current = s_visual_levels[distance];
        int target = targets[distance];
        s_visual_levels[distance] = target > current ? (current + target * 2) / 3
                                                      : (current * 7 + target) / 8;
    }

    for (int i = 0; i < 17; i++) {
        int distance = i < 8 ? 8 - i : i - 8;
        int value = s_visual_levels[distance];
        int bar_h = 3 + value * (max_h - 3) / 100;
        int accent_mix = 215 - distance * 14 + value * 40 / 100;
        if (accent_mix > 255) accent_mix = 255;
        rect.bg_color = lv_color_mix(C_ACCENT, C_PAPER, accent_mix);
        lv_canvas_draw_rect(canvas, start_x + i * (bar_w + gap),
                            (height - bar_h) / 2, bar_w, bar_h, &rect);
    }
}

// 画布只是借用此缓冲，必须在播放页（含画布）销毁后才能释放。
void ui_visual_destroy(void)
{
    visual_canvas = NULL;
    if (visual_buffer) heap_caps_free(visual_buffer);
    visual_buffer = NULL;
    memset(s_visual_levels, 0, sizeof(s_visual_levels));
}
