#pragma once
#include "ui_types.h"

// 内部接口：调用方持有 LVGL 锁。页面对象不向其它模块暴露可写句柄。
void ui_widgets_plain(lv_obj_t *o);
lv_obj_t *ui_widgets_mk_box(lv_obj_t *par, lv_coord_t w, lv_coord_t h);
lv_obj_t *ui_widgets_mk_label(lv_obj_t *par, const char *txt, const lv_font_t *f, lv_color_t c);
lv_obj_t *ui_widgets_mk_btn(lv_obj_t *par, const char *txt, const lv_font_t *f, bool primary,
                        lv_event_cb_t cb, void *ud);
lv_obj_t *ui_widgets_mk_favorite_btn(lv_obj_t *parent, bool checked,
                                  lv_event_cb_t callback, void *user_data);
void ui_widgets_add_statebox(lv_obj_t *par, const char *t1, const char *t2,
                         const char *a1, lv_event_cb_t cb1,
                         const char *a2, lv_event_cb_t cb2);
lv_obj_t *ui_widgets_setting_nav_row(lv_obj_t *body, const char *name, const char *value,
                                 lv_event_cb_t cb, lv_obj_t **value_out);
lv_obj_t *ui_widgets_build_page_header(lv_obj_t *screen, const char *title);
lv_obj_t *ui_widgets_build_small_icon(lv_obj_t *parent, small_icon_t kind);
