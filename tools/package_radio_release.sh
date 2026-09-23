#!/usr/bin/env bash
# 构建并打包 Radio OS 正式发布产物；不执行签名、部署、提交或打 tag。
# 用法：tools/package_radio_release.sh 1.1.6
#
# 产物落在 dist/（不入库，由 CI 上传到 GitHub Releases）。
# OTA 发布说明是本脚本的输入而不是产物，所以留在入库位置 docs/releases/。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-}"

if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "用法：tools/package_radio_release.sh <主版本.次版本.修订号>" >&2
    exit 2
fi

PROJECT_VERSION="$(sed -nE 's/^set\(PROJECT_VER "([^"]+)".*$/\1/p' "$ROOT/CMakeLists.txt")"
if [[ "$PROJECT_VERSION" != "$VERSION" ]]; then
    echo "版本不一致：PROJECT_VER=${PROJECT_VERSION}，要求打包=$VERSION" >&2
    exit 3
fi

NOTES_FILE="$ROOT/docs/releases/radio-os-$VERSION-ota-notes.txt"
if [[ ! -f "$NOTES_FILE" ]]; then
    echo "缺少 OTA 发布说明：$NOTES_FILE" >&2
    exit 4
fi
if (( $(wc -c < "$NOTES_FILE") > 191 )); then
    echo "OTA 发布说明超过固件可接收的 191 字节" >&2
    exit 5
fi

RELEASE_DIR="$ROOT/dist/radio-os-$VERSION"
mkdir -p "$RELEASE_DIR"
# 不能继承开发回归时的 CMake 缓存；发布镜像不得带触摸注入/截图探针。
"$ROOT/tools/idf.sh" -D RADIO_UI_TEST=OFF build
cp "$ROOT/build/radio.bin" "$RELEASE_DIR/radio-os-$VERSION-ota.bin"
"$ROOT/tools/idf.sh" merge-bin -f raw -o "$RELEASE_DIR/radio-os-$VERSION-0x0.bin"

(
    cd "$RELEASE_DIR"
    shasum -a 256 "radio-os-$VERSION-ota.bin" "radio-os-$VERSION-0x0.bin" > SHA256SUMS
)

echo "Radio OS $VERSION 发布包已生成：$RELEASE_DIR"
cat "$RELEASE_DIR/SHA256SUMS"
