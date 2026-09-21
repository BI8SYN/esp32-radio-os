#!/usr/bin/env bash
# 重新生成固件用的中文字库（LVGL 8 格式）。
#
#   tools/gen_fonts.sh                  # 用钉死版本的 Noto Sans SC
#   tools/gen_fonts.sh 路径/字体.ttf     # 用指定字体（任意 TTF/OTF 都通用）
#
# 字符集 = GB2312 一级汉字 + 固件实际文案/全部电台名 + 常用标点，共 3807 个字符。
#
# 为什么要定重：Noto Sans SC 是可变字体，wght 轴的默认实例是 100（Thin）。
# 直接喂给 lv_font_conv 会得到极细体，整个界面观感塌掉。所以先用 fontTools
# 把它固定到一个字重再转。
#
# 为什么是 400 而不是 500：Noto Sans CJK 的 Medium(500) 比同名 Medium 字重的其他黑体
# 偏重，再加上 14px 下 bpp=2 只有 4 级灰度抗锯齿，笔画多的汉字会糊成一团。500 与 400
# 都烧上板子比过，400（Regular）观感合适。想再调就改下面的 FONT_WEIGHT，这是可变字体，
# 100-900 连续可选，改完重跑本脚本即可。
#
# 注意：lv_font_conv 1.5.x 生成的是 LVGL 8 格式，所以固件锁在 LVGL 8.4。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/main/fonts"
CACHE="$ROOT/tools/font-src"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# google/fonts 里这个文件自 2022-12-09 起未再改动。按 commit 钉死而不是跟 main，
# 这样任何人任何时候重跑都得到同一份字库。
FONT_COMMIT="2894aab31764f10f29c421bdfd2340d3b382d384"
FONT_URL="https://raw.githubusercontent.com/google/fonts/${FONT_COMMIT}/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf"
FONT_SHA256="a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da"
FONT_WEIGHT="400"
VF="$CACHE/NotoSansSC-VF.ttf"
STATIC="$CACHE/NotoSansSC-w${FONT_WEIGHT}.ttf"

SRC_FONT="${1:-}"
if [ -z "$SRC_FONT" ]; then
    if ! python3 -c "import fontTools" 2>/dev/null; then
        echo "缺少 fontTools（用于给可变字体定重）。装一下：" >&2
        echo "    python3 -m pip install fonttools" >&2
        exit 1
    fi

    mkdir -p "$CACHE"
    if [ ! -f "$VF" ]; then
        echo "下载 Noto Sans SC 可变字体（约 17MB，OFL 1.1）…"
        curl -fsSL -o "$VF" "$FONT_URL"
    fi

    echo "校验字体摘要…"
    actual="$(shasum -a 256 "$VF" | awk '{print $1}')"
    if [ "$actual" != "$FONT_SHA256" ]; then
        echo "字体摘要不匹配。期望 ${FONT_SHA256}，实得 $actual" >&2
        echo "删掉 $VF 重试；若仍不匹配，说明上游文件被改动过，先核实再继续。" >&2
        exit 1
    fi

    if [ ! -f "$STATIC" ]; then
        echo "定重到 wght=${FONT_WEIGHT}…"
        python3 - "$VF" "$STATIC" "$FONT_WEIGHT" <<'PY'
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

src, dst, weight = sys.argv[1], sys.argv[2], float(sys.argv[3])
font = TTFont(src)
inst = instancer.instantiateVariableFont(
    font, {"wght": weight}, inplace=False, updateFontNames=True)
print("  →", inst["name"].getDebugName(4))
inst.save(dst)
PY
    fi
    SRC_FONT="$STATIC"
fi

echo "生成字符集…"
python3 - "$WORK/symbols.txt" "$ROOT/main" <<'PY'
import re
import sys
from pathlib import Path
chars = []
for hi in range(0xB0, 0xD8):          # GB2312 一级汉字区 B0A1–D7F9
    for lo in range(0xA1, 0xFF):
        try:
            chars.append(bytes([hi, lo]).decode('gb2312'))
        except UnicodeDecodeError:
            pass
punct = "·•，。、：；？！“”‘’（）《》〈〉「」—…～％＋－×÷℃　"
extra = "①②③④⑤⑥⑦⑧⑨⑩"
root = Path(sys.argv[2])
# 将所有实际 C 字符串里的汉字纳入字库。这样新增电台名时不会再次因为
# GB2312 边界字（如“颍、圳、崂、暨、漯、濮、盱、眙、衢、邳、魅”）出现方块。
actual = []
for path in [root / "ui.c", root / "stations.c", root / "station_catalog.inc"]:
    text = path.read_text(encoding="utf-8")
    for token in re.findall(r'"(?:\\.|[^"\\])*"', text):
        actual.extend(ch for ch in token if 0x3400 <= ord(ch) <= 0x9FFF)
seen, out = set(), []
for ch in chars + actual + list(punct) + list(extra):
    if ch not in seen:
        seen.add(ch); out.append(ch)
open(sys.argv[1], 'w', encoding='utf-8').write(''.join(out))
print(f"  {len(out)} 个字符")
PY

# lv_font_conv 会把完整命令行写进生成文件的头部注释。用绝对路径调用会把开发机
# 家目录（/Users/...）带进公开仓库，所以这里切到仓库根、把 --font 与 -o 都换成
# 相对路径再调用。
SRC_FONT_ABS="$(python3 -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "$SRC_FONT")"
REL_FONT="$(python3 -c 'import os, sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))' "$SRC_FONT_ABS" "$ROOT")"
REL_OUT="$(python3 -c 'import os, sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))' "$OUT" "$ROOT")"

SYMS="$(cat "$WORK/symbols.txt")"
for sz in 14 18 24; do
    echo "生成 ${sz}px…"
    (cd "$ROOT" && npx --yes lv_font_conv@1.5.3 \
        --font "$REL_FONT" --size "$sz" --bpp 2 --no-compress \
        --format lvgl --lv-include lvgl.h \
        --range 0x20-0x7E --symbols "$SYMS" \
        -o "$REL_OUT/font_cjk_${sz}.c")
    ls -lh "$OUT/font_cjk_${sz}.c" | awk '{print "  →", $9, $5}'
done

echo
echo "度量（换字体后要和这几个数对上，否则界面垂直位置会变）："
for sz in 14 18 24; do
    lh="$(grep -oE '\.line_height = [0-9]+' "$OUT/font_cjk_${sz}.c" | grep -oE '[0-9]+')"
    bl="$(grep -oE '\.base_line = [0-9]+' "$OUT/font_cjk_${sz}.c" | grep -oE '[0-9]+')"
    echo "  ${sz}px: line_height=${lh} base_line=${bl}"
done

echo
echo "完成。字库符号名：font_cjk_14 / font_cjk_18 / font_cjk_24"
