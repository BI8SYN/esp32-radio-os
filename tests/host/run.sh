#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I tests/host/stubs -I main main/app_radio.c main/preferences.c tests/host/test_app.c \
    -o "$test_dir/test_app"
"$test_dir/test_app"
