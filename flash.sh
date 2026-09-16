#!/bin/bash
# Flash what build.sh produced with the host esptool (Docker on macOS cannot reach USB). Usage: ./flash.sh [port]
cd "$(dirname "$0")"
PORT="${1:-$(ls /dev/cu.usbmodem* | head -1)}"
cd build_docker && esptool --chip esp32s3 --port "$PORT" --baud 921600 write_flash @flash_args
