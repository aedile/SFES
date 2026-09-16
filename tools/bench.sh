#!/bin/bash
# Build with the given -D knobs, flash, and print the fps lines from 22 s of log. Usage: tools/bench.sh -DMEM_LAYOUT=2 -DAPU_OFF=1
cd "$(dirname "$0")/.."
PY=$(ls /opt/homebrew/Cellar/esptool/*/libexec/bin/python | head -1)
./build.sh "$@" build 2>&1 | grep -E "error|sfes.bin binary" || exit 1
./flash.sh >/dev/null 2>&1 || { echo flash failed; exit 1; }
$PY tools/monitor.py 22 | grep -E "SFES: (bench|heap after|[0-9.]+ fps)" | sed 's/.*SFES: //'
