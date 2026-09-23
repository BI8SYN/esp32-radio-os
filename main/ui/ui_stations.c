// 列表和筛选页共用已提交筛选；s_draft 是页面草稿，取消时不写回应用状态。
#include "ui_internal.h"
static const char *TAG = "ui_stations";

static int filter_active_count(const station_filter_t *f);
static void on_row_click(lv_event_t *e);
static void on_heart_click(lv_event_t *e);
static void on_pager_click(lv_event_t *e);
static void on_prev_page(lv_event_t *e);
static void on_clear_filter(lv_event_t *e);
static void on_goto_preset(lv_event_t *e);
static void add_row(lv_obj_t *par, const station_t *st, int idx);
static void on_tab_click(lv_event_t *e);
static void refresh_chips(void);
static void on_chip_cat(lv_event_t *e);
static void on_region_change(lv_event_t *e);
static void on_filter_open(lv_event_t *e);
static void on_filter_cancel(lv_event_t *e);
static void on_filter_reset(lv_event_t *e);
static void on_filter_apply(lv_event_t *e);
static void build_chip_group(lv_obj_t *par, const char *title, int count,
                             const char *(*label_fn)(int), int (*count_fn)(int),
                             lv_event_cb_t cb, lv_obj_t ***store);

static lv_obj_t *scr_list, *scr_filter;
static lv_obj_t *list_content, *btn_filter, *lbl_filter, *lbl_title;
static lv_obj_t *tab_btn[3], *tab_lbl[3], *mini_name, *mini_state, *mini_icon;
static lv_obj_t **chip_cat, *region_dropdown;
static station_filter_t s_draft;
static int filter_active_count(const station_filter_t *f)
{
    return (f->cat != 0) + (f->region != 0);
}

void ui_stations_refresh_minibar(void)
{
    if (!mini_name) return;
    if (app_radio_state()->has_station) {
        lv_label_set_text(mini_name, app_radio_state()->play_station.name);
    } else {
        lv_label_set_text(mini_name, "未选择电台");
    }
    player_state_t ps = player_state();
    const char *t = "已停止";
    if (ps == PLAYER_PLAYING) t = "播放中";
    else if (ps == PLAYER_BUFFERING) t = "缓冲中…";
    else if (ps == PLAYER_ERROR) t = "失败";
    lv_label_set_text(mini_state, t);
    lv_label_set_text(mini_icon, ps == PLAYER_PLAYING ? LV_SYMBOL_PLAY : LV_SYMBOL_STOP);
}

static void on_row_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    app_radio_play_station(app_radio_state()->tab, idx);
    ui_navigate_locked(UI_SCREEN_PLAYER);
    ui_player_refresh();
}

static void on_heart_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const station_t *st = app_radio_source_get(app_radio_state()->tab, idx);
    if (!st) return;
    if (stations_fav_contains(st->url)) {
        stations_fav_remove(st->url);
    } else {
        if (stations_fav_add(st) == ESP_ERR_NO_MEM) {
            ESP_LOGW(TAG, "收藏已满");
        }
    }
    ui_stations_refresh_list();
}

static void on_pager_click(lv_event_t *e)
{
    (void)e;
    int pages = (app_radio_source_count(STATION_SRC_CATALOG) + CATALOG_PAGE_SIZE - 1) / CATALOG_PAGE_SIZE;
    if (app_radio_state()->catalog_page + 1 < pages) ++app_radio_state()->catalog_page;
    ui_stations_refresh_list();
}

static void on_prev_page(lv_event_t *e)
{
    (void)e;
    if (app_radio_state()->catalog_page > 0) --app_radio_state()->catalog_page;
    ui_stations_refresh_list();
}

static void on_clear_filter(lv_event_t *e)
{
    (void)e;
    app_radio_state()->filter.cat = app_radio_state()->filter.region = 0;
    app_radio_state()->catalog_page = 0;
    ui_stations_refresh_list();
}

static void on_goto_preset(lv_event_t *e)
{
    (void)e;
    app_radio_state()->tab = STATION_SRC_PRESET;
    preferences_save_tab();
    ui_stations_refresh_list();
}

static void add_row(lv_obj_t *par, const station_t *st, int idx)
{
    bool playing = app_radio_state()->has_station && strcmp(st->url, app_radio_state()->play_station.url) == 0;
    bool fav = stations_fav_contains(st->url);

    lv_obj_t *row = ui_widgets_mk_box(par, LV_PCT(100), ROW_H);
    lv_obj_set_style_bg_color(row, playing ? C_TINT : C_SURFACE, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_border_width(row, playing ? 1 : 0, 0);
    lv_obj_set_style_border_color(row, C_ACCENT, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, on_row_click, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    // 正在播放：靠左边一道竖线标识（省一个子对象）
    if (playing) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(row, C_ACCENT, 0);
        lv_obj_set_style_border_width(row, 3, 0);
    }

    lv_obj_t *name = ui_widgets_mk_label(row, st->name, &font_cjk_18, C_INK);
    // LONG_DOT 只有在宽高都受约束时才会截断；仅设宽度会先增高并换行。
    lv_obj_set_size(name, 194, 22);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(name, 8, 2);

    lv_obj_t *meta = ui_widgets_mk_label(row, st->meta, &font_cjk_14, C_INK2);
    lv_obj_set_size(meta, 194, 18);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(meta, 8, 24);

    lv_obj_t *heart = ui_widgets_mk_favorite_btn(row, fav, on_heart_click, (void *)(intptr_t)idx);
    lv_obj_set_size(heart, 44, ROW_H - 2);
    lv_obj_align(heart, LV_ALIGN_RIGHT_MID, -2, 1);
}

void ui_stations_refresh_list(void)
{
    if (!list_content) return;

    lv_obj_clean(list_content);

    const char *titles[3] = { "我的收藏", "编辑精选", "国内电台" };
    lv_label_set_text(lbl_title, titles[app_radio_state()->tab]);

    // tab 选中态
    for (int i = 0; i < 3; i++) {
        bool on = (i == (int)app_radio_state()->tab);
        lv_obj_set_style_bg_color(tab_btn[i], on ? C_ACCENT : C_PAPER, 0);
        lv_obj_set_style_text_color(tab_lbl[i], on ? C_ON_ACCENT : C_INK2, 0);
    }

    // 完整台库可按内容类型与省份筛选；精选和收藏保持一步直达。
    if (app_radio_state()->tab == STATION_SRC_CATALOG) {
        lv_obj_clear_flag(btn_filter, LV_OBJ_FLAG_HIDDEN);
        int n = filter_active_count(&app_radio_state()->filter);
        char buf[24];
        if (n > 0) snprintf(buf, sizeof(buf), "筛选·%d", n);
        else snprintf(buf, sizeof(buf), "筛选");
        lv_label_set_text(lbl_filter, buf);
    } else {
        lv_obj_add_flag(btn_filter, LV_OBJ_FLAG_HIDDEN);
    }

    if (app_radio_state()->tab == STATION_SRC_FAV && stations_fav_count() == 0) {
        ui_widgets_add_statebox(list_content, "还没有收藏电台", "在精选或台库里点心形按钮收藏",
                     "去看精选", on_goto_preset, NULL, NULL);
        return;
    }

    int n = app_radio_source_count(app_radio_state()->tab);
    if (app_radio_state()->tab == STATION_SRC_CATALOG && n == 0) {
        ui_widgets_add_statebox(list_content, "没有符合条件的电台", "换个地区或分类试试",
                     "清除筛选", on_clear_filter, NULL, NULL);
        return;
    }

    int first = app_radio_state()->tab == STATION_SRC_CATALOG ? app_radio_state()->catalog_page * CATALOG_PAGE_SIZE : 0;
    int last = app_radio_state()->tab == STATION_SRC_CATALOG ? first + CATALOG_PAGE_SIZE : n;
    if (last > n) last = n;
    for (int i = first; i < last; i++) {
        const station_t *st = app_radio_source_get(app_radio_state()->tab, i);
        if (st) add_row(list_content, st, i);
    }

    if (app_radio_state()->tab == STATION_SRC_CATALOG) {
        int pages = (n + CATALOG_PAGE_SIZE - 1) / CATALOG_PAGE_SIZE;
        lv_obj_t *pager = ui_widgets_mk_box(list_content, LV_PCT(100), 40);
        lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pager, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(pager, 8, 0);
        lv_obj_t *prev = ui_widgets_mk_btn(pager, LV_SYMBOL_LEFT, &lv_font_montserrat_14, false,
                                on_prev_page, NULL);
        lv_obj_set_size(prev, 44, 34);
        if (app_radio_state()->catalog_page == 0) lv_obj_add_state(prev, LV_STATE_DISABLED);
        char buf[48];
        snprintf(buf, sizeof(buf), "%d / %d · 共 %d 台", app_radio_state()->catalog_page + 1, pages, n);
        lv_obj_t *page = ui_widgets_mk_label(pager, buf, &font_cjk_14, C_INK2);
        lv_obj_set_width(page, 120);
        lv_obj_set_style_text_align(page, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *next = ui_widgets_mk_btn(pager, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, false,
                                on_pager_click, NULL);
        lv_obj_set_size(next, 44, 34);
        if (app_radio_state()->catalog_page + 1 >= pages) lv_obj_add_state(next, LV_STATE_DISABLED);
    }
}

static void on_tab_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    app_radio_state()->tab = (station_src_t)idx;
    preferences_save_tab();
    app_radio_state()->catalog_page = 0;
    ui_stations_refresh_list();
}

static void refresh_chips(void)
{
    for (int i = 0; i < stations_cat_count(); i++) {
        bool on = (i == s_draft.cat);
        lv_obj_set_style_bg_color(chip_cat[i], on ? C_ACCENT : C_SURFACE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(chip_cat[i], 0), on ? C_ON_ACCENT : C_INK, 0);
    }
    if (region_dropdown) lv_dropdown_set_selected(region_dropdown, s_draft.region);
}

static void on_chip_cat(lv_event_t *e)  { s_draft.cat  = (int)(intptr_t)lv_event_get_user_data(e); refresh_chips(); }

static void on_region_change(lv_event_t *e)
{
    s_draft.region = lv_dropdown_get_selected(lv_event_get_target(e));
}

static void on_filter_open(lv_event_t *e)
{
    (void)e;
    s_draft = app_radio_state()->filter;
    refresh_chips();
    ui_navigate_locked(UI_SCREEN_FILTER);
}

static void on_filter_cancel(lv_event_t *e)
{
    (void)e;
    ui_navigate_locked(UI_SCREEN_STATIONS);
}

static void on_filter_reset(lv_event_t *e)
{
    (void)e;
    s_draft.cat = s_draft.region = 0;
    refresh_chips();
}

static void on_filter_apply(lv_event_t *e)
{
    (void)e;
    app_radio_state()->filter = s_draft;
    app_radio_state()->catalog_page = 0;
    ui_stations_refresh_list();
    ui_navigate_locked(UI_SCREEN_STATIONS);
}

void ui_stations_build(void)
{
    scr_list = lv_obj_create(NULL);
    ui_widgets_plain(scr_list);
    lv_obj_set_flex_flow(scr_list, LV_FLEX_FLOW_COLUMN);

    // 头部
    lv_obj_t *hdr = ui_widgets_mk_box(scr_list, LV_PCT(100), HEADER_H);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, C_LINE, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);

    lv_obj_t *back = ui_widgets_mk_btn(hdr, LV_SYMBOL_LEFT, &lv_font_montserrat_14, false, ui_player_on_goto_player, NULL);
    lv_obj_set_size(back, 48, HEADER_H - 1);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);

    lbl_title = ui_widgets_mk_label(hdr, "电台列表", &font_cjk_18, C_INK);
    lv_obj_center(lbl_title);

    btn_filter = ui_widgets_mk_btn(hdr, "筛选", &font_cjk_14, false, on_filter_open, NULL);
    lv_obj_set_size(btn_filter, 56, HEADER_H - 1);
    lv_obj_set_style_border_width(btn_filter, 0, 0);
    lv_obj_align(btn_filter, LV_ALIGN_RIGHT_MID, 0, 0);
    lbl_filter = lv_obj_get_child(btn_filter, 0);

    // 主体：左侧竖排 tab + 右侧列表
    lv_obj_t *body = ui_widgets_mk_box(scr_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);

    lv_obj_t *tabs = ui_widgets_mk_box(body, 60, LV_PCT(100));
    lv_obj_set_style_border_side(tabs, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(tabs, C_LINE, 0);
    lv_obj_set_style_border_width(tabs, 1, 0);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_COLUMN);

    static const char *tab_names[3] = { "收藏", "精选", "台库" };
    // 顺序与 station_src_t 一致：FAV=0, PRESET=1, CATALOG=2
    for (int i = 0; i < 3; i++) {
        tab_btn[i] = ui_widgets_mk_btn(tabs, tab_names[i], &font_cjk_14, false, on_tab_click, (void *)(intptr_t)i);
        lv_obj_set_width(tab_btn[i], 59);
        lv_obj_set_flex_grow(tab_btn[i], 1);
        lv_obj_set_style_border_width(tab_btn[i], 0, 0);
        lv_obj_set_style_border_side(tab_btn[i], LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(tab_btn[i], C_LINE, 0);
        lv_obj_set_style_border_width(tab_btn[i], 1, 0);
        tab_lbl[i] = lv_obj_get_child(tab_btn[i], 0);
    }

    list_content = ui_widgets_mk_box(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(list_content, 1);
    lv_obj_set_flex_flow(list_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(list_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(list_content, 5, 0);
    lv_obj_set_style_pad_row(list_content, 4, 0);

    // 底部迷你播放条
    lv_obj_t *mini = ui_widgets_mk_box(scr_list, LV_PCT(100), MINIBAR_H);
    lv_obj_set_style_border_side(mini, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(mini, C_INK, 0);
    lv_obj_set_style_border_width(mini, 1, 0);
    lv_obj_add_flag(mini, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mini, ui_player_on_goto_player, LV_EVENT_CLICKED, NULL);

    lv_obj_set_style_bg_color(mini, C_SURFACE, 0);
    mini_icon = ui_widgets_mk_label(mini, LV_SYMBOL_STOP, &lv_font_montserrat_14, C_ACCENT);
    lv_obj_align(mini_icon, LV_ALIGN_LEFT_MID, 8, 0);

    mini_name = ui_widgets_mk_label(mini, "未选择电台", &font_cjk_14, C_INK);
    lv_obj_set_size(mini_name, 200, 18);
    lv_label_set_long_mode(mini_name, LV_LABEL_LONG_DOT);
    lv_obj_align(mini_name, LV_ALIGN_LEFT_MID, 30, 0);

    mini_state = ui_widgets_mk_label(mini, "已停止", &font_cjk_14, C_INK2);
    lv_obj_align(mini_state, LV_ALIGN_RIGHT_MID, -8, 0);
}

static void build_chip_group(lv_obj_t *par, const char *title, int count,
                             const char *(*label_fn)(int), int (*count_fn)(int),
                             lv_event_cb_t cb, lv_obj_t ***store)
{
    lv_obj_t *lbl = ui_widgets_mk_label(par, title, &font_cjk_14, C_INK2);
    lv_obj_set_style_pad_top(lbl, 4, 0);

    lv_obj_t *wrap = ui_widgets_mk_box(par, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(wrap, 6, 0);
    lv_obj_set_style_pad_row(wrap, 6, 0);

    *store = lv_mem_alloc(sizeof(lv_obj_t *) * count);
    for (int i = 0; i < count; i++) {
        char text[32];
        int available = count_fn(i);
        snprintf(text, sizeof(text), "%s %d", label_fn(i), available);
        lv_obj_t *c = ui_widgets_mk_btn(wrap, text, &font_cjk_14, false, cb, (void *)(intptr_t)i);
        lv_obj_set_size(c, LV_SIZE_CONTENT, 30);
        lv_obj_set_style_pad_left(c, 10, 0);
        lv_obj_set_style_pad_right(c, 10, 0);
        if (available == 0) lv_obj_add_state(c, LV_STATE_DISABLED);
        (*store)[i] = c;
    }
}

void ui_stations_build_filter(void)
{
    scr_filter = lv_obj_create(NULL);
    ui_widgets_plain(scr_filter);
    lv_obj_set_flex_flow(scr_filter, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *hdr = ui_widgets_mk_box(scr_filter, LV_PCT(100), HEADER_H);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, C_LINE, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);

    lv_obj_t *close = ui_widgets_mk_btn(hdr, LV_SYMBOL_CLOSE, &lv_font_montserrat_14, false, on_filter_cancel, NULL);
    lv_obj_set_size(close, 48, HEADER_H - 1);
    lv_obj_set_style_border_width(close, 0, 0);
    lv_obj_align(close, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *t = ui_widgets_mk_label(hdr, "筛选", &font_cjk_18, C_INK);
    lv_obj_center(t);

    lv_obj_t *reset = ui_widgets_mk_btn(hdr, "重置", &font_cjk_14, false, on_filter_reset, NULL);
    lv_obj_set_size(reset, 48, HEADER_H - 1);
    lv_obj_set_style_border_width(reset, 0, 0);
    lv_obj_align(reset, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *content = ui_widgets_mk_box(scr_filter, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(content, 8, 0);
    lv_obj_set_style_pad_right(content, 8, 0);
    lv_obj_set_style_pad_row(content, 3, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);

    build_chip_group(content, "内容分类", stations_cat_count(), stations_cat_label,
                     stations_cat_station_count,
                     on_chip_cat, &chip_cat);
    ui_widgets_mk_label(content, "地区", &font_cjk_14, C_INK2);
    char region_options[1024] = { 0 };
    size_t used = 0;
    for (int i = 0; i < stations_region_count(); ++i) {
        int n = snprintf(region_options + used, sizeof(region_options) - used, "%s %d%s",
                         stations_region_label(i), stations_region_station_count(i),
                         i + 1 < stations_region_count() ? "\n" : "");
        if (n < 0 || (size_t)n >= sizeof(region_options) - used) break;
        used += (size_t)n;
    }
    region_dropdown = lv_dropdown_create(content);
    lv_obj_set_size(region_dropdown, LV_PCT(100), 38);
    lv_dropdown_set_options(region_dropdown, region_options);
    // 默认下拉箭头是 Font Awesome 私有区字符，中文字体无法绘制。
    // 关闭内建符号并用矢量折线绘制，避免“全部 542”右侧出现豆腐块。
    lv_dropdown_set_symbol(region_dropdown, NULL);
    lv_obj_set_style_text_font(region_dropdown, &font_cjk_14, 0);
    lv_obj_set_style_text_color(region_dropdown, C_INK, 0);
    lv_obj_set_style_bg_color(region_dropdown, C_SURFACE, 0);
    lv_obj_set_style_border_color(region_dropdown, C_LINE, 0);
    lv_obj_set_style_radius(region_dropdown, 10, 0);
    lv_obj_set_style_pad_right(region_dropdown, 30, LV_PART_MAIN);
    lv_obj_add_event_cb(region_dropdown, on_region_change, LV_EVENT_VALUE_CHANGED, NULL);
    static const lv_point_t down_points[] = {{0, 1}, {5, 6}, {10, 1}};
    lv_obj_t *down = lv_line_create(region_dropdown);
    lv_line_set_points(down, down_points, 3);
    lv_obj_set_size(down, 11, 7);
    lv_obj_set_style_line_color(down, C_INK2, 0);
    lv_obj_set_style_line_width(down, 2, 0);
    lv_obj_set_style_line_rounded(down, true, 0);
    lv_obj_align(down, LV_ALIGN_RIGHT_MID, -10, 0);
    // 下拉列表是独立对象，必须显式设为深色，避免复现旧版黑白极性错乱。
    lv_dropdown_open(region_dropdown);
    lv_obj_t *region_list = lv_dropdown_get_list(region_dropdown);
    if (region_list) {
        lv_obj_set_style_bg_color(region_list, C_SURFACE, 0);
        lv_obj_set_style_text_color(region_list, C_INK, 0);
        lv_obj_set_style_text_font(region_list, &font_cjk_14, 0);
        lv_obj_set_style_border_color(region_list, C_LINE, 0);
        lv_obj_set_style_bg_color(region_list, C_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(region_list, C_ON_ACCENT,
                                    LV_PART_SELECTED | LV_STATE_CHECKED);
    }
    lv_dropdown_close(region_dropdown);

    lv_obj_t *bottom = ui_widgets_mk_box(scr_filter, LV_PCT(100), 52);
    lv_obj_set_style_border_side(bottom, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(bottom, C_LINE, 0);
    lv_obj_set_style_border_width(bottom, 1, 0);
    lv_obj_set_style_pad_all(bottom, 6, 0);
    lv_obj_t *apply = ui_widgets_mk_btn(bottom, "应用", &font_cjk_14, true, on_filter_apply, NULL);
    lv_obj_set_size(apply, LV_PCT(100), 40);
}

lv_obj_t *ui_stations_screen(ui_screen_t id)
{
    switch (id) {
    case UI_SCREEN_STATIONS: return scr_list;
    case UI_SCREEN_FILTER: return scr_filter;
    default: return NULL;
    }
}

// 仅在已切到过渡屏幕后调用：父屏幕先销毁子控件，再清空本模块句柄。
void ui_stations_destroy(void)
{
    if (scr_list) lv_obj_del(scr_list);
    if (scr_filter) lv_obj_del(scr_filter);
    if (chip_cat) lv_mem_free(chip_cat);
    scr_list = NULL;
    scr_filter = NULL;
    list_content = NULL;
    btn_filter = NULL;
    lbl_filter = NULL;
    lbl_title = NULL;
    memset(tab_btn, 0, sizeof(tab_btn));
    memset(tab_lbl, 0, sizeof(tab_lbl));
    mini_name = NULL;
    mini_state = NULL;
    mini_icon = NULL;
    chip_cat = NULL;
    region_dropdown = NULL;
}

void ui_stations_prepare_filter(void) { s_draft = app_radio_state()->filter; refresh_chips(); }
