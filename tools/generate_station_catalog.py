#!/usr/bin/env python3
"""Build the deterministic on-device China radio catalog.

The firmware never contacts a directory service.  This maintenance tool turns a
public playlist snapshot into a compact C include and can optionally restrict it
to a TSV produced by the stream health probe used during a release.
"""

from __future__ import annotations

import argparse
import re
import urllib.request
from pathlib import Path


SOURCE_URL = "https://raw.githubusercontent.com/gaotianliuyun/gao/master/radio.txt"

REGIONS = [
    ("全国", "NATIONAL", ("CNR", "CRI", "中国", "中华", "环球", "Asia", "清晨", "华语", "民歌")),
    ("北京", "BEIJING", ("北京",)),
    ("天津", "TIANJIN", ("天津",)),
    ("河北", "HEBEI", ("河北", "石家庄", "保定", "唐山", "秦皇岛", "邯郸", "邢台", "沧州", "衡水", "廊坊", "承德")),
    ("山西", "SHANXI", ("山西", "太原", "大同", "阳泉", "长治", "晋城", "朔州", "晋中", "运城", "忻州", "临汾", "吕梁")),
    ("内蒙古", "INNER_MONGOLIA", ("内蒙古", "呼和浩特", "包头", "乌海", "赤峰", "通辽", "鄂尔多斯", "呼伦贝尔", "巴彦淖尔", "乌兰察布", "兴安盟", "锡林郭勒", "阿拉善")),
    ("辽宁", "LIAONING", ("辽宁", "沈阳", "大连", "鞍山", "抚顺", "本溪", "丹东", "锦州", "营口", "阜新", "辽阳", "盘锦", "铁岭", "朝阳", "葫芦岛")),
    ("吉林", "JILIN", ("吉林", "长春", "四平", "辽源", "通化", "白山", "松原", "白城", "延边")),
    ("黑龙江", "HEILONGJIANG", ("黑龙江", "哈尔滨", "齐齐哈尔", "鸡西", "鹤岗", "双鸭山", "大庆", "伊春", "佳木斯", "七台河", "牡丹江", "黑河", "绥化", "大兴安岭")),
    ("上海", "SHANGHAI", ("上海", "东广", "第一财经", "动感101", "经典947", "Love Radio")),
    ("江苏", "JIANGSU", ("江苏", "南京", "无锡", "徐州", "常州", "苏州", "南通", "连云港", "淮安", "盐城", "扬州", "镇江", "泰州", "宿迁", "江阴", "昆山", "张家港", "常熟", "宜兴", "扬中")),
    ("浙江", "ZHEJIANG", ("浙江", "杭州", "宁波", "温州", "嘉兴", "湖州", "绍兴", "金华", "衢州", "舟山", "台州", "丽水", "义乌")),
    ("安徽", "ANHUI", ("安徽", "合肥", "芜湖", "蚌埠", "淮南", "马鞍山", "淮北", "铜陵", "安庆", "黄山", "滁州", "阜阳", "宿州", "六安", "亳州", "池州", "宣城")),
    ("福建", "FUJIAN", ("福建", "福州", "厦门", "莆田", "三明", "泉州", "漳州", "南平", "龙岩", "宁德", "晋江")),
    ("江西", "JIANGXI", ("江西", "南昌", "景德镇", "萍乡", "九江", "新余", "鹰潭", "赣州", "吉安", "宜春", "抚州", "上饶")),
    ("山东", "SHANDONG", ("山东", "济南", "青岛", "淄博", "枣庄", "东营", "烟台", "潍坊", "济宁", "泰安", "威海", "日照", "临沂", "德州", "聊城", "滨州", "菏泽")),
    ("河南", "HENAN", ("河南", "郑州", "开封", "洛阳", "平顶山", "安阳", "鹤壁", "新乡", "焦作", "濮阳", "许昌", "漯河", "三门峡", "南阳", "商丘", "信阳", "周口", "驻马店", "济源")),
    ("湖北", "HUBEI", ("湖北", "武汉", "黄石", "十堰", "宜昌", "襄阳", "鄂州", "荆门", "孝感", "荆州", "黄冈", "咸宁", "随州", "恩施", "仙桃", "潜江", "天门", "神农架", "楚天", "公安")),
    ("湖南", "HUNAN", ("湖南", "长沙", "株洲", "湘潭", "衡阳", "邵阳", "岳阳", "常德", "张家界", "益阳", "郴州", "永州", "怀化", "娄底", "湘西")),
    ("广东", "GUANGDONG", ("广东", "广州", "韶关", "深圳", "珠海", "汕头", "佛山", "江门", "湛江", "茂名", "肇庆", "惠州", "梅州", "汕尾", "河源", "阳江", "清远", "东莞", "中山", "潮州", "揭阳", "云浮", "恩平", "南粤", "珠江")),
    ("广西", "GUANGXI", ("广西", "南宁", "柳州", "桂林", "梧州", "北海", "防城港", "钦州", "贵港", "玉林", "百色", "贺州", "河池", "来宾", "崇左", "北部湾")),
    ("海南", "HAINAN", ("海南", "海口", "三亚", "三沙", "儋州")),
    ("重庆", "CHONGQING", ("重庆", "巴渝")),
    ("四川", "SICHUAN", ("四川", "成都", "自贡", "攀枝花", "泸州", "德阳", "绵阳", "广元", "遂宁", "内江", "乐山", "南充", "眉山", "宜宾", "广安", "达州", "雅安", "巴中", "资阳", "阿坝", "甘孜", "凉山")),
    ("贵州", "GUIZHOU", ("贵州", "贵阳", "六盘水", "遵义", "安顺", "毕节", "铜仁", "黔西南", "黔东南", "黔南")),
    ("云南", "YUNNAN", ("云南", "昆明", "曲靖", "玉溪", "保山", "昭通", "丽江", "普洱", "临沧", "楚雄", "红河", "文山", "西双版纳", "大理", "德宏", "怒江", "迪庆")),
    ("西藏", "TIBET", ("西藏", "拉萨", "日喀则", "昌都", "林芝", "山南", "那曲", "阿里", "康巴")),
    ("陕西", "SHAANXI", ("陕西", "西安", "铜川", "宝鸡", "咸阳", "渭南", "延安", "汉中", "榆林", "安康", "商洛")),
    ("甘肃", "GANSU", ("甘肃", "兰州", "嘉峪关", "金昌", "白银", "天水", "武威", "张掖", "平凉", "酒泉", "庆阳", "定西", "陇南", "临夏", "甘南")),
    ("青海", "QINGHAI", ("青海", "西宁", "海东", "海北", "黄南", "海南州", "果洛", "玉树", "海西")),
    ("宁夏", "NINGXIA", ("宁夏", "银川", "石嘴山", "吴忠", "固原", "中卫")),
    ("新疆", "XINJIANG", ("新疆", "乌鲁木齐", "克拉玛依", "吐鲁番", "哈密", "昌吉", "博尔塔拉", "巴音郭楞", "阿克苏", "克孜勒苏", "喀什", "和田", "伊犁", "塔城", "阿勒泰", "石河子")),
    ("香港", "HONG_KONG", ("香港", "RTHK")),
    ("澳门", "MACAO", ("澳门",)),
    ("台湾", "TAIWAN", ("台湾", "台北", "高雄", "台中", "台南")),
]

CATEGORIES = [
    ("交通", "TRAFFIC", ("交通", "汽车", "私家车", "爱车", "车生活", "车友")),
    ("财经", "FINANCE", ("财经", "经济", "股市", "财富", "证券")),
    ("新闻", "NEWS", ("新闻", "资讯", "时政")),
    ("音乐", "MUSIC", ("音乐", "金曲", "动感", "经典", "怀旧", "好声音", "流行", "摇滚", "民歌", "MYFM", "Love Radio", "Asia FM", "AsiaFM")),
    ("文艺", "CULTURE", ("文艺", "戏曲", "故事", "评书", "相声", "阅读", "文化", "书香")),
    ("体育", "SPORTS", ("体育", "运动")),
    ("少儿", "KIDS", ("少儿", "儿童", "校园", "教育", "科教")),
    ("方言", "DIALECT", ("方言", "粤语", "闽南", "客家", "藏语", "康巴", "蒙古语", "维吾尔", "哈萨克", "朝鲜语")),
    ("生活", "LIFE", ("生活", "都市", "旅游", "健康", "女性", "女主播", "民生", "农村", "农民", "老年", "老朋友", "服务")),
]

FEATURED_IDS = {
    "15318317", "386", "20500172", "5022038", "4915", "3412131", "339", "332", "333",
    "270", "276", "274", "267", "266", "1254", "1259", "1260", "1262", "1270", "1271",
    "1272", "1753", "1758", "1861", "4911", "1498", "1500", "1644", "1646", "23933",
    "23927", "1290", "1291", "4877", "1220", "1222", "4879", "1287", "1134", "1133",
    "2801", "2802", "1661", "1662", "1698", "1702", "5021912", "4581",
}

# 2026-09-06 发布前全量复检确认持续返回 HTTP 404。保留在生成器里，避免下一次
# 从上游候选列表重建目录时把已知失效项重新带回固件。
RELEASE_EXCLUDED_IDS = {
    "1282", "1283", "2123", "2138", "2792", "4891", "5083", "20150", "21001",
    "15318224", "15318307", "20211604", "5021761", "5022004", "5022070", "5022388",
    "5022389",
}


def enum_for(name: str, table: list[tuple[str, str, tuple[str, ...]]], fallback: str) -> str:
    # Specific geographic names must beat generic national words such as "中国".
    rows = table[1:] + table[:1] if table is REGIONS else table
    for _label, enum, words in rows:
        if any(word.lower() in name.lower() for word in words):
            return enum
    return fallback


def label_for(enum: str, table: list[tuple[str, str, tuple[str, ...]]], fallback: str) -> str:
    return next((label for label, key, _ in table if key == enum), fallback)


def c_string(value: str, max_bytes: int) -> str:
    data = value.encode("utf-8")[: max_bytes - 1]
    while True:
        try:
            value = data.decode("utf-8")
            break
        except UnicodeDecodeError:
            data = data[:-1]
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--verified-tsv", type=Path)
    ap.add_argument("--enriched-tsv", type=Path,
                    help="release TSV: id, official title, stream, region, city, Qingting category, ok")
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()

    verified: set[str] | None = None
    if args.verified_tsv:
        verified = {line.split("\t", 1)[0] for line in args.verified_tsv.read_text().splitlines() if line}

    enriched: dict[str, tuple[str, str, str]] = {}
    if args.enriched_tsv:
        for row in args.enriched_tsv.read_text(encoding="utf-8").splitlines():
            station_id, title, _stream, region, _city, category, ok = row.split("\t")
            if ok == "True":
                enriched[station_id] = (title, region, category)

    text = urllib.request.urlopen(SOURCE_URL, timeout=20).read().decode("utf-8-sig")
    entries: list[tuple[str, str, str, str, str, str, int]] = []
    seen: set[str] = set()
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.endswith(",#genre#") or "," not in line:
            continue
        name, url = line.rsplit(",", 1)
        match = re.search(r"qingting\.fm/live/(\d+)", url)
        if not match:
            continue
        station_id = match.group(1)
        if (station_id in seen or station_id in RELEASE_EXCLUDED_IDS or
                (verified is not None and station_id not in verified)):
            continue
        seen.add(station_id)
        if station_id in enriched:
            official_title, official_region, official_category = enriched[station_id]
            name = official_title
            region = next((key for label, key, _ in REGIONS if label == official_region), "NATIONAL")
            category = {
                "综合台": "GENERAL", "资讯台": "NEWS", "音乐台": "MUSIC",
                "交通台": "TRAFFIC", "经济台": "FINANCE", "文艺台": "CULTURE",
                "曲艺台": "CULTURE", "生活台": "LIFE", "都市台": "LIFE",
                "旅游台": "LIFE", "体育台": "SPORTS", "方言台": "DIALECT",
                "双语台": "DIALECT",
            }.get(official_category, "GENERAL")
        else:
            region = enum_for(name, REGIONS, "NATIONAL")
            category = enum_for(name, CATEGORIES, "GENERAL")
        region_label = label_for(region, REGIONS, "全国")
        category_label = label_for(category, CATEGORIES, "综合")
        stream = f"http://lhttp.qtfm.cn/live/{station_id}/64k.mp3"
        entries.append((station_id, name.strip(), stream, region, category, f"{region_label} · {category_label}", station_id in FEATURED_IDS))

    entries.sort(key=lambda x: (x[3] != "NATIONAL", x[3], x[4], x[1]))
    lines = [
        "/* Generated by tools/generate_station_catalog.py; do not hand-edit. */",
        f"/* Source: {SOURCE_URL} + RTHK public MP3 | release probe accepted {len(entries) + 5} streams. */",
    ]
    for station_id, name, stream, region, category, meta, featured in entries:
        lines.append(
            "    { { %s, %s, %s }, STATION_REGION_%s, STATION_CAT_%s, %d }, /* %s */"
            % (c_string(name, 64), c_string(stream, 192), c_string(meta, 48), region, category, featured, station_id)
        )
    lines.extend([
        '    { { "RTHK Radio 1", "http://stm.rthk.hk/radio1", "香港 · 新闻" }, STATION_REGION_HONG_KONG, STATION_CAT_NEWS, 1 },',
        '    { { "RTHK Radio 2", "http://stm.rthk.hk/radio2", "香港 · 综合" }, STATION_REGION_HONG_KONG, STATION_CAT_GENERAL, 1 },',
        '    { { "RTHK Radio 3", "http://stm.rthk.hk/radio3", "香港 · 英语" }, STATION_REGION_HONG_KONG, STATION_CAT_DIALECT, 0 },',
        '    { { "RTHK Radio 4", "http://stm.rthk.hk/radio4", "香港 · 音乐" }, STATION_REGION_HONG_KONG, STATION_CAT_MUSIC, 0 },',
        '    { { "RTHK Radio 5", "http://stm.rthk.hk/radio5", "香港 · 文艺" }, STATION_REGION_HONG_KONG, STATION_CAT_CULTURE, 0 },',
    ])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"generated {len(entries) + 5} stations -> {args.output}")


if __name__ == "__main__":
    main()
