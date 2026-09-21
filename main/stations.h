// 电台数据：固件内置的国内目录 / 精选 / 收藏（NVS）。运行时不请求第三方目录。
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STATION_NAME_MAX 64
#define STATION_URL_MAX  192
#define STATION_META_MAX 48
#define STATION_FAV_MAX  50

typedef struct {
    char name[STATION_NAME_MAX];
    char url[STATION_URL_MAX];
    char meta[STATION_META_MAX];
} station_t;

typedef enum {
    STATION_SRC_FAV = 0,
    STATION_SRC_PRESET,
    STATION_SRC_CATALOG,
} station_src_t;

typedef enum {
    STATION_REGION_ALL = 0,
    STATION_REGION_NATIONAL,
    STATION_REGION_BEIJING,
    STATION_REGION_TIANJIN,
    STATION_REGION_HEBEI,
    STATION_REGION_SHANXI,
    STATION_REGION_INNER_MONGOLIA,
    STATION_REGION_LIAONING,
    STATION_REGION_JILIN,
    STATION_REGION_HEILONGJIANG,
    STATION_REGION_SHANGHAI,
    STATION_REGION_JIANGSU,
    STATION_REGION_ZHEJIANG,
    STATION_REGION_ANHUI,
    STATION_REGION_FUJIAN,
    STATION_REGION_JIANGXI,
    STATION_REGION_SHANDONG,
    STATION_REGION_HENAN,
    STATION_REGION_HUBEI,
    STATION_REGION_HUNAN,
    STATION_REGION_GUANGDONG,
    STATION_REGION_GUANGXI,
    STATION_REGION_HAINAN,
    STATION_REGION_CHONGQING,
    STATION_REGION_SICHUAN,
    STATION_REGION_GUIZHOU,
    STATION_REGION_YUNNAN,
    STATION_REGION_TIBET,
    STATION_REGION_SHAANXI,
    STATION_REGION_GANSU,
    STATION_REGION_QINGHAI,
    STATION_REGION_NINGXIA,
    STATION_REGION_XINJIANG,
    STATION_REGION_HONG_KONG,
    STATION_REGION_MACAO,
    STATION_REGION_TAIWAN,
    STATION_REGION_COUNT,
} station_region_t;

typedef enum {
    STATION_CAT_ALL = 0,
    STATION_CAT_GENERAL,
    STATION_CAT_NEWS,
    STATION_CAT_MUSIC,
    STATION_CAT_TRAFFIC,
    STATION_CAT_FINANCE,
    STATION_CAT_CULTURE,
    STATION_CAT_LIFE,
    STATION_CAT_SPORTS,
    STATION_CAT_KIDS,
    STATION_CAT_DIALECT,
    STATION_CAT_COUNT,
} station_category_t;

typedef struct {
    int region;
    int cat;
} station_filter_t;

esp_err_t stations_init(void);

int stations_preset_count(void);
const station_t *stations_preset_get(int idx);

int stations_catalog_count(void);
const station_t *stations_catalog_get(int idx);
int stations_catalog_filtered_count(const station_filter_t *filter);
const station_t *stations_catalog_filtered_get(const station_filter_t *filter, int nth,
                                                int *catalog_idx);
const char *stations_catalog_region_label(int catalog_idx);
const char *stations_catalog_category_label(int catalog_idx);

int stations_region_count(void);
const char *stations_region_label(int idx);
int stations_region_station_count(int idx);
int stations_cat_count(void);
const char *stations_cat_label(int idx);
int stations_cat_station_count(int idx);

int stations_fav_count(void);
const station_t *stations_fav_get(int idx);
bool stations_fav_contains(const char *url);
esp_err_t stations_fav_add(const station_t *st);
esp_err_t stations_fav_remove(const char *url);

#ifdef __cplusplus
}
#endif
