#!/bin/sh
# Build and flash MobileMash firmware via PlatformIO.
#
# Usage: ./flash.sh [env] [build]
#   ./flash.sh                       # build + upload default env (esp32)
#   ./flash.sh build                 # build only, default env
#   ./flash.sh seeed    # build + upload XIAO ESP32-C6
#   ./flash.sh seeed build   # build only, XIAO ESP32-C6
#
# Valid envs (see platformio.ini): esp32, seeed
set -e

cd "$(dirname "$0")"

ENV=esp32
BUILD_ONLY=

for arg in "$@"; do
    case "$arg" in
        build) BUILD_ONLY=1 ;;
        *)     ENV="$arg" ;;
    esac
done

if [ -n "$BUILD_ONLY" ]; then
    python3 -m platformio run -e "$ENV"
else
    python3 -m platformio run -e "$ENV" --target upload
fi
