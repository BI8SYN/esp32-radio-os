#!/usr/bin/env bash
# 通用 esptool 包装：统一串口 / 波特率 / 芯片型号。
# 用法： tools/esp.sh <esptool 子命令> [参数...]
#   例： tools/esp.sh flash_id
#
# 这块板子（LCDwiki ES3C28P）用的是 ESP32-S3 内置 USB-Serial/JTAG，
# 串口通常叫 /dev/cu.usbmodemXXXXX（不是 CH340 的 usbserial）。
# 原生 USB 不受 UART 波特率限制，可以直接跑 921600。
set -euo pipefail

PORT="${ESP_PORT:-}"
if [[ -z "$PORT" ]]; then
  # 自动挑第一个 usbmodem（S3 原生 USB），找不到再退回 usbserial（外接 USB-TTL）
  PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
  [[ -z "$PORT" ]] && PORT="$(ls /dev/cu.usbserial* 2>/dev/null | head -1 || true)"
fi

if [[ -z "$PORT" || ! -e "$PORT" ]]; then
  echo "错误：找不到串口。请插好 USB-C 线，或显式设置 ESP_PORT=/dev/cu.usbmodemXXXXX" >&2
  ls /dev/cu.* 2>/dev/null >&2 || true
  exit 1
fi

BAUD="${ESP_BAUD:-921600}"

# esptool 来源：优先 ESP-IDF 环境里的，其次 PATH 上的
if command -v esptool.py >/dev/null 2>&1; then
  exec esptool.py --port "$PORT" --baud "$BAUD" --chip esp32s3 "$@"
elif [[ -f "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" ]]; then
  # shellcheck disable=SC1091
  source "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" >/dev/null 2>&1
  exec esptool.py --port "$PORT" --baud "$BAUD" --chip esp32s3 "$@"
else
  echo "错误：找不到 esptool.py。先 source ~/esp/esp-idf/export.sh，或 pip install esptool" >&2
  exit 1
fi
