#!/bin/sh
# Run every host-side test for the high-speed flash changes.
#
# The two C++ tests are registered as cc_test_host in tests/Android.bp, but
# running them here does not need a full Android build. The two Python tests
# extract the real GUI/install code and compile it, so they need a C++
# compiler at runtime; set CXX to override auto-detection.
set -eu

cd "$(dirname "$0")/.."

CXX_BIN=${CXX:-}
if [ -z "$CXX_BIN" ]; then
    for candidate in c++ clang++ g++; do
        if command -v "$candidate" >/dev/null 2>&1; then
            CXX_BIN=$candidate
            break
        fi
    done
fi
if [ -z "$CXX_BIN" ]; then
    echo "No C++ compiler found; install one or set CXX=/path/to/c++" >&2
    exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== OTA device check ==="
"$CXX_BIN" -std=c++17 -Wall -Wextra -Werror \
    -I twrpinstall/include tests/ota_device_check_test.cpp -o "$tmp/ota_device_check"
"$tmp/ota_device_check"

echo "=== Flash review layout/consent ==="
"$CXX_BIN" -std=c++17 -Wall -Wextra -Werror \
    -I . -I twrpinstall/include tests/flash_review_test.cpp -o "$tmp/flash_review"
"$tmp/flash_review"

echo "=== Theme navigation ==="
python3 tests/test_flash_review_navigation.py

echo "=== Sideload review ==="
python3 tests/test_sideload_review.py

echo "All host tests passed."
