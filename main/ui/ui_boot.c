// 启动页由淡出动画回收；过渡完成后才允许主题重建，避免回收中的屏幕被再次切换。
#include "ui_internal.h"

static void anim_set_opa(void *object, int32_t value);
static void anim_set_y(void *object, int32_t value);
static void anim_set_width(void *object, int32_t value);
static void anim_set_boot_wave(void *object, int32_t value);
static void draw_boot_mark(lv_event_t *e);
static void start_boot_anim(lv_obj_t *object, lv_anim_exec_xcb_t exec,
                            int from, int to, int duration, int delay);
static void boot_finish_cb(lv_timer_t *timer);
static void boot_transition_done_cb(lv_timer_t *timer);

static lv_obj_t *scr_boot;
static int s_boot_wave;
static void anim_set_opa(void *object, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)value, 0);
}

static void anim_set_y(void *object, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)object, (lv_coord_t)value);
}

static void anim_set_width(void *object, int32_t value)
{
    lv_obj_set_width((lv_obj_t *)object, (lv_coord_t)value);
    lv_obj_align((lv_obj_t *)object, LV_ALIGN_TOP_MID, 0, 181);
}

static void anim_set_boot_wave(void *object, int32_t value)
{
    s_boot_wave = value;
    lv_obj_invalidate((lv_obj_t *)object);
}

static void draw_boot_mark(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const lv_coord_t cx = (area.x1 + area.x2) / 2;
    const lv_coord_t cy = (area.y1 + area.y2) / 2;
    static const int8_t final_heights[] = {12, 24, 34, 24, 12};
    lv_draw_rect_dsc_t bar;
    lv_draw_rect_dsc_init(&bar);
    bar.bg_color = C_ACCENT;
    bar.radius = LV_RADIUS_CIRCLE;
    for (int i = 0; i < 5; ++i) {
        int delayed = s_boot_wave - i * 8;
        if (delayed < 0) delayed = 0;
        if (delayed > 100) delayed = 100;
        lv_coord_t height = 3 + (final_heights[i] - 3) * delayed / 100;
        lv_area_t rect = {
            cx - 15 + i * 7, cy - height / 2,
            cx - 12 + i * 7, cy + height / 2,
        };
        lv_draw_rect(ctx, &bar, &rect);
    }
}

static void start_boot_anim(lv_obj_t *object, lv_anim_exec_xcb_t exec,
                            int from, int to, int duration, int delay)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_time(&animation, duration);
    lv_anim_set_delay(&animation, delay);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation, exec);
    lv_anim_start(&animation);
}

static void boot_finish_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!scr_boot) return;
    lv_obj_t *boot = scr_boot;
    scr_boot = NULL;
    if (lv_scr_act() == boot) {
        ui_player_refresh();
        ui_player_refresh_wifi();
        lv_scr_load_anim(ui_page_screen(UI_SCREEN_PLAYER), LV_SCR_LOAD_ANIM_FADE_ON, BOOT_FADE_MS, 0, true);
        lv_timer_t *done = lv_timer_create(boot_transition_done_cb, BOOT_FADE_MS + 40, NULL);
        lv_timer_set_repeat_count(done, 1);
    } else {
        lv_obj_del(boot);
        ui_boot_ready();
    }
}

static void boot_transition_done_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_boot_ready();
}

void ui_boot_build(void)
{
    scr_boot = lv_obj_create(NULL);
    ui_widgets_plain(scr_boot);

    lv_obj_t *mark = ui_widgets_mk_box(scr_boot, 56, 56);
    lv_obj_set_pos(mark, 132, 56);
    lv_obj_set_style_radius(mark, 18, 0);
    lv_obj_set_style_bg_color(mark, C_TINT, 0);
    lv_obj_set_style_opa(mark, LV_OPA_0, 0);
    lv_obj_add_event_cb(mark, draw_boot_mark, LV_EVENT_DRAW_MAIN_END, NULL);

    lv_obj_t *title = ui_widgets_mk_label(scr_boot, "Radio OS", &lv_font_montserrat_20, C_INK);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 126);
    lv_obj_set_style_opa(title, LV_OPA_0, 0);

    lv_obj_t *signature = ui_widgets_mk_label(scr_boot, "powered by BI8SYN",
                                   &lv_font_montserrat_12, C_INK2);
    lv_obj_set_style_text_letter_space(signature, 1, 0);
    lv_obj_align(signature, LV_ALIGN_TOP_MID, 0, 154);
    lv_obj_set_style_opa(signature, LV_OPA_0, 0);

    lv_obj_t *line = ui_widgets_mk_box(scr_boot, 1, 2);
    lv_obj_set_style_radius(line, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(line, C_ACCENT, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 181);

    s_boot_wave = 0;
    start_boot_anim(mark, anim_set_opa, LV_OPA_0, LV_OPA_COVER, 380, 80);
    start_boot_anim(mark, anim_set_y, 66, 56, 520, 80);
    start_boot_anim(mark, anim_set_boot_wave, 0, 100, 620, 140);
    start_boot_anim(title, anim_set_opa, LV_OPA_0, LV_OPA_COVER, 420, 360);
    start_boot_anim(title, anim_set_y, 136, 126, 500, 320);
    start_boot_anim(signature, anim_set_opa, LV_OPA_0, LV_OPA_COVER, 420, 620);
    start_boot_anim(signature, anim_set_y, 163, 154, 500, 580);
    start_boot_anim(line, anim_set_width, 1, 72, 600, 720);
}

lv_obj_t *ui_boot_screen(ui_screen_t id)
{
    switch (id) {
    case UI_SCREEN_BOOT: return scr_boot;
    default: return NULL;
    }
}

void ui_boot_start_timer(void)
{
    lv_timer_t *timer = lv_timer_create(boot_finish_cb, BOOT_HOLD_MS, NULL);
    lv_timer_set_repeat_count(timer, 1);
}
