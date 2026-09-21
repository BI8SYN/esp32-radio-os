#include "stations.h"

#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "stations";

#define NVS_NS      "radio"
#define NVS_KEY_FAV "favs"

typedef struct {
    station_t station;
    uint8_t region;
    uint8_t category;
    uint8_t featured;
} catalog_entry_t;

static const char *const s_region_labels[] = {
    "全部", "全国", "北京", "天津", "河北", "山西", "内蒙古", "辽宁", "吉林",
    "黑龙江", "上海", "江苏", "浙江", "安徽", "福建", "江西", "山东", "河南",
    "湖北", "湖南", "广东", "广西", "海南", "重庆", "四川", "贵州", "云南",
    "西藏", "陕西", "甘肃", "青海", "宁夏", "新疆", "香港", "澳门", "台湾",
};

static const char *const s_category_labels[] = {
    "全部", "综合", "新闻", "音乐", "交通", "财经", "文艺", "生活", "体育", "少儿", "方言",
};

static const catalog_entry_t s_catalog[] = {
#include "station_catalog.inc"
};

static station_t s_favs[STATION_FAV_MAX];
static int s_fav_count;

_Static_assert(sizeof(s_region_labels) / sizeof(s_region_labels[0]) == STATION_REGION_COUNT,
               "station region label table mismatch");
_Static_assert(sizeof(s_category_labels) / sizeof(s_category_labels[0]) == STATION_CAT_COUNT,
               "station category label table mismatch");

static bool catalog_match(const catalog_entry_t *entry, const station_filter_t *filter)
{
    if (!filter) return true;
    if (filter->region > STATION_REGION_ALL && entry->region != filter->region) return false;
    if (filter->cat > STATION_CAT_ALL && entry->category != filter->cat) return false;
    return true;
}

int stations_catalog_count(void)
{
    return (int)(sizeof(s_catalog) / sizeof(s_catalog[0]));
}

const station_t *stations_catalog_get(int idx)
{
    if (idx < 0 || idx >= stations_catalog_count()) return NULL;
    return &s_catalog[idx].station;
}

int stations_catalog_filtered_count(const station_filter_t *filter)
{
    int count = 0;
    for (int i = 0; i < stations_catalog_count(); ++i) {
        if (catalog_match(&s_catalog[i], filter)) ++count;
    }
    return count;
}

const station_t *stations_catalog_filtered_get(const station_filter_t *filter, int nth,
                                                int *catalog_idx)
{
    if (nth < 0) return NULL;
    for (int i = 0, found = 0; i < stations_catalog_count(); ++i) {
        if (!catalog_match(&s_catalog[i], filter)) continue;
        if (found++ == nth) {
            if (catalog_idx) *catalog_idx = i;
            return &s_catalog[i].station;
        }
    }
    return NULL;
}

const char *stations_catalog_region_label(int catalog_idx)
{
    if (catalog_idx < 0 || catalog_idx >= stations_catalog_count()) return "";
    return s_region_labels[s_catalog[catalog_idx].region];
}

const char *stations_catalog_category_label(int catalog_idx)
{
    if (catalog_idx < 0 || catalog_idx >= stations_catalog_count()) return "";
    return s_category_labels[s_catalog[catalog_idx].category];
}

int stations_preset_count(void)
{
    int count = 0;
    for (int i = 0; i < stations_catalog_count(); ++i) count += s_catalog[i].featured != 0;
    return count;
}

const station_t *stations_preset_get(int idx)
{
    if (idx < 0) return NULL;
    for (int i = 0, found = 0; i < stations_catalog_count(); ++i) {
        if (!s_catalog[i].featured) continue;
        if (found++ == idx) return &s_catalog[i].station;
    }
    return NULL;
}

int stations_region_count(void) { return STATION_REGION_COUNT; }
const char *stations_region_label(int idx)
{
    return (idx >= 0 && idx < STATION_REGION_COUNT) ? s_region_labels[idx] : "";
}

int stations_region_station_count(int idx)
{
    if (idx == STATION_REGION_ALL) return stations_catalog_count();
    if (idx < 0 || idx >= STATION_REGION_COUNT) return 0;
    int count = 0;
    for (int i = 0; i < stations_catalog_count(); ++i) count += s_catalog[i].region == idx;
    return count;
}

int stations_cat_count(void) { return STATION_CAT_COUNT; }
const char *stations_cat_label(int idx)
{
    return (idx >= 0 && idx < STATION_CAT_COUNT) ? s_category_labels[idx] : "";
}

int stations_cat_station_count(int idx)
{
    if (idx == STATION_CAT_ALL) return stations_catalog_count();
    if (idx < 0 || idx >= STATION_CAT_COUNT) return 0;
    int count = 0;
    for (int i = 0; i < stations_catalog_count(); ++i) count += s_catalog[i].category == idx;
    return count;
}

static void fav_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (s_fav_count > 0) {
        nvs_set_blob(h, NVS_KEY_FAV, s_favs, (size_t)s_fav_count * sizeof(station_t));
    } else {
        nvs_erase_key(h, NVS_KEY_FAV);
    }
    nvs_commit(h);
    nvs_close(h);
}

static void fav_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_favs);
    if (nvs_get_blob(h, NVS_KEY_FAV, s_favs, &len) == ESP_OK && len % sizeof(station_t) == 0) {
        s_fav_count = (int)(len / sizeof(station_t));
        if (s_fav_count > STATION_FAV_MAX) s_fav_count = STATION_FAV_MAX;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "收藏已加载：%d 个", s_fav_count);
}

int stations_fav_count(void) { return s_fav_count; }
const station_t *stations_fav_get(int idx)
{
    return (idx >= 0 && idx < s_fav_count) ? &s_favs[idx] : NULL;
}

bool stations_fav_contains(const char *url)
{
    if (!url) return false;
    for (int i = 0; i < s_fav_count; ++i) {
        if (strcmp(s_favs[i].url, url) == 0) return true;
    }
    return false;
}

esp_err_t stations_fav_add(const station_t *st)
{
    if (!st) return ESP_ERR_INVALID_ARG;
    if (stations_fav_contains(st->url)) return ESP_OK;
    if (s_fav_count >= STATION_FAV_MAX) return ESP_ERR_NO_MEM;
    s_favs[s_fav_count++] = *st;
    fav_save();
    return ESP_OK;
}

esp_err_t stations_fav_remove(const char *url)
{
    if (!url) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < s_fav_count; ++i) {
        if (strcmp(s_favs[i].url, url) != 0) continue;
        memmove(&s_favs[i], &s_favs[i + 1], (size_t)(s_fav_count - i - 1) * sizeof(station_t));
        --s_fav_count;
        fav_save();
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t stations_init(void)
{
    fav_load();
    ESP_LOGI(TAG, "国内离线目录：%d 个台，精选 %d 个；运行时目录请求已禁用",
             stations_catalog_count(), stations_preset_count());
    return ESP_OK;
}
