// 无页面私有状态的控件工厂；需要业务动作时由调用方传入回调。
#include "ui_internal.h"
#include "assets/heart_icon.h"

static void draw_favorite_heart(lv_event_t *e);
static void draw_small_icon(lv_event_t *e);

void ui_widgets_plain(lv_obj_t *o)
{
    lv_obj_set_style_bg_color(o, C_PAPER, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *ui_widgets_mk_box(lv_obj_t *par, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *o = lv_obj_create(par);
    ui_widgets_plain(o);
    lv_obj_set_size(o, w, h);
    return o;
}

lv_obj_t *ui_widgets_mk_label(lv_obj_t *par, const char *txt, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

lv_obj_t *ui_widgets_mk_btn(lv_obj_t *par, const char *txt, const lv_font_t *f, bool primary,
                        lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_btn_create(par);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, primary ? C_ACCENT : C_SURFACE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_bg_color(b, primary ? lv_color_darken(C_ACCENT, LV_OPA_20) : C_RAISED,
                              LV_STATE_PRESSED);
    lv_obj_t *l = ui_widgets_mk_label(b, txt, f, primary ? C_ON_ACCENT : C_INK);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

static void draw_favorite_heart(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const lv_coord_t cx = (area.x1 + area.x2) / 2;
    const lv_coord_t cy = (area.y1 + area.y2) / 2;
    bool checked = (lv_obj_get_state(obj) & LV_STATE_CHECKED) != 0;
    lv_draw_img_dsc_t icon;
    lv_draw_img_dsc_init(&icon);
    icon.recolor = checked ? C_ACCENT : C_INK2;
    icon.recolor_opa = LV_OPA_COVER;
    icon.opa = LV_OPA_COVER;
    // 轮廓本身略偏下，绘制区域向上做 1px 光学修正。
    lv_area_t icon_area = {cx - 12, cy - 13, cx + 11, cy + 10};
    lv_draw_img(ctx, &icon, &icon_area,
                checked ? &heart_icon_filled : &heart_icon_outline);
}

lv_obj_t *ui_widgets_mk_favorite_btn(lv_obj_t *parent, bool checked,
                                  lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(button, C_TINT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, C_TINT, LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    if (checked) lv_obj_add_state(button, LV_STATE_CHECKED);
    lv_obj_add_event_cb(button, draw_favorite_heart, LV_EVENT_DRAW_MAIN_END, NULL);
    if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    return button;
}

void ui_widgets_add_statebox(lv_obj_t *par, const char *t1, const char *t2,
                         const char *a1, lv_event_cb_t cb1,
                         const char *a2, lv_event_cb_t cb2)
{
    lv_obj_t *box = ui_widgets_mk_box(par, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(box, 18, 0);
    lv_obj_set_style_pad_left(box, 10, 0);
    lv_obj_set_style_pad_right(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 6, 0);

    lv_obj_t *l1 = ui_widgets_mk_label(box, t1, &font_cjk_18, C_INK);
    lv_obj_set_style_text_align(l1, LV_TEXT_ALIGN_CENTER, 0);

    if (t2 && t2[0]) {
        lv_obj_t *l2 = ui_widgets_mk_label(box, t2, &font_cjk_14, C_INK2);
        lv_obj_set_width(l2, 220);
        lv_label_set_long_mode(l2, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(l2, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (a1) {
        lv_obj_t *acts = ui_widgets_mk_box(box, LV_SIZE_CONTENT, 36);
        lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(acts, 8, 0);
        lv_obj_t *b1 = ui_widgets_mk_btn(acts, a1, &font_cjk_14, true, cb1, NULL);
        lv_obj_set_size(b1, 104, 36);
        if (a2) {
            lv_obj_t *b2 = ui_widgets_mk_btn(acts, a2, &font_cjk_14, false, cb2, NULL);
            lv_obj_set_size(b2, 104, 36);
        }
    }
}

lv_obj_t *ui_widgets_setting_nav_row(lv_obj_t *body, const char *name, const char *value,
                                 lv_event_cb_t cb, lv_obj_t **value_out)
{
    if (value_out) *value_out = NULL;
    lv_obj_t *row = lv_btn_create(body);
    lv_obj_set_size(row, LV_PCT(100), ROW_H);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, C_LINE, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_bg_color(row, C_PAPER, 0);
    lv_obj_set_style_bg_color(row, C_TINT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_t *name_label = ui_widgets_mk_label(row, name, &font_cjk_14, C_INK);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 8, 0);
    if (value) {
        lv_obj_t *value_label = ui_widgets_mk_label(row, value, &font_cjk_14, C_INK2);
        if (value_out) *value_out = value_label;
        lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, cb ? -22 : -8, 0);

    }
    if (cb) {
        lv_obj_t *arrow = ui_widgets_mk_label(row, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, C_INK2);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -7, 0);
        lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, NULL);
    } else {
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    }
    return row;
}

lv_obj_t *ui_widgets_build_page_header(lv_obj_t *screen, const char *title)
{
    lv_obj_t *hdr = ui_widgets_mk_box(screen, LV_PCT(100), HEADER_H);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, C_LINE, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_t *back = ui_widgets_mk_btn(hdr, LV_SYMBOL_LEFT, &lv_font_montserrat_14,
                            false, ui_back_to_settings, NULL);
    lv_obj_set_size(back, 48, HEADER_H - 1);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *label = ui_widgets_mk_label(hdr, title, &font_cjk_18, C_INK);
    lv_obj_center(label);
    return hdr;
}

static void draw_small_icon(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    small_icon_t kind = (small_icon_t)(intptr_t)lv_event_get_user_data(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    lv_color_t color = kind == SMALL_ICON_TIMER ? C_ACCENT : C_INK2;
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_TRANSP;
    rect.border_color = color;
    rect.border_width = kind == SMALL_ICON_TIMER ? 1 : 2;
    rect.radius = kind == SMALL_ICON_TIMER ? LV_RADIUS_CIRCLE : 3;
    lv_area_t outline = kind == SMALL_ICON_TIMER
        ? (lv_area_t){area.x1, area.y1, area.x2, area.y2}
        : (lv_area_t){area.x1 + 1, area.y1 + 2, area.x2 - 2, area.y2 - 6};
    lv_draw_rect(ctx, &rect, &outline);

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = kind == SMALL_ICON_TIMER ? 1 : 2;
    line.round_start = true;
    line.round_end = true;
    if (kind == SMALL_ICON_TIMER) {
        lv_coord_t cx = (area.x1 + area.x2) / 2;
        lv_coord_t cy = (area.y1 + area.y2) / 2;
        lv_point_t hour[2] = {{cx, cy}, {cx, cy - 4}};
        lv_point_t minute[2] = {{cx, cy}, {cx + 4, cy}};
        lv_draw_line(ctx, &line, &hour[0], &hour[1]);
        lv_draw_line(ctx, &line, &minute[0], &minute[1]);
    } else {
        lv_coord_t cx = (area.x1 + area.x2) / 2;
        lv_point_t stand[2] = {{cx - 4, area.y2 - 2}, {cx + 4, area.y2 - 2}};
        lv_draw_line(ctx, &line, &stand[0], &stand[1]);
    }
}

lv_obj_t *ui_widgets_build_small_icon(lv_obj_t *parent, small_icon_t kind)
{
    lv_obj_t *icon = ui_widgets_mk_box(parent, kind == SMALL_ICON_TIMER ? 14 : 20,
                           kind == SMALL_ICON_TIMER ? 14 : 20);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(icon, draw_small_icon, LV_EVENT_DRAW_MAIN_END,
                        (void *)(intptr_t)kind);
    return icon;
}
