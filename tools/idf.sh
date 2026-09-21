#!/usr/bin/env bash
# idf.py 包装：激活 ESP-IDF 环境后在固件工程目录里执行 idf.py。
#
#   tools/idf.sh build
#   tools/idf.sh -p /dev/cu.usbmodem21201 flash monitor
#
# 为什么需要这个包装：ESP-IDF 的 export.sh 会按**当前系统 python3 的版本号**
# 去拼虚拟环境路径（形如 idf5.5_py3.11_env）。一旦系统 python 升级（例如
# 3.11 → 3.13），它就找不到当初建好的 venv 而直接报错。这里主动探测实际存在
# 的 venv 并用 IDF_PYTHON_ENV_PATH 指过去，避免每台机器都要手动折腾。
set -euo pipefail

IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"
export IDF_PATH

if [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "找不到 ESP-IDF：$IDF_PATH" >&2
    echo "装好后再试，或设 IDF_PATH 指向你的安装位置。" >&2
    exit 1
fi

# 没有显式指定就自动挑一个可用的 venv：优先匹配当前 IDF 版本，其次任意可用的。
if [ -z "${IDF_PYTHON_ENV_PATH:-}" ]; then
    idf_ver="$(git -C "$IDF_PATH" describe --tags 2>/dev/null | sed -E 's/^v([0-9]+\.[0-9]+).*/\1/')"
    for env_dir in "$HOME/.espressif/python_env/idf${idf_ver}_py3."*_env \
                   "$HOME/.espressif/python_env/"idf*_py3.*_env; do
        if [ -x "$env_dir/bin/python" ]; then
            export IDF_PYTHON_ENV_PATH="$env_dir"
            break
        fi
    done
fi

# shellcheck disable=SC1091
source "$IDF_PATH/export.sh" >/dev/null 2>&1

PROJECT_DIR="${RADIO_PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
cd "$PROJECT_DIR"
exec idf.py "$@"
