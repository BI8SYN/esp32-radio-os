#!/usr/bin/env bash
# 完整备份 16MB Flash + eFuse 摘要，输出到 backup/<标签>/。
# 用法： tools/backup_flash.sh [标签]     标签默认为当前时间戳
set -euo pipefail

cd "$(dirname "$0")/.."
LABEL="${1:-$(date +%Y%m%d-%H%M%S)}"
OUT="backup/$LABEL"
mkdir -p "$OUT"

echo "==> 读取芯片信息"
tools/esp.sh flash_id | tee "$OUT/chip_info.txt"

echo "==> 读取 MAC"
tools/esp.sh read_mac | tee -a "$OUT/chip_info.txt"

echo "==> 读取整片 16MB Flash（921600 下约 3 分钟，请勿拔线）"
tools/esp.sh read_flash 0x0 0x1000000 "$OUT/flash_full_16MB.bin"

echo "==> 计算校验和"
shasum -a 256 "$OUT/flash_full_16MB.bin" | tee "$OUT/SHA256SUMS"

echo "==> 完成：$OUT/flash_full_16MB.bin"
