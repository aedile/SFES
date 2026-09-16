#!/bin/bash
# Build in Espressif's Docker image (no host ESP-IDF needed). Extra args go to idf.py, e.g. ./build.sh -DROM="Super Mario All-Stars + Super Mario World.zip" build
cd "$(dirname "$0")"
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 idf.py -B build_docker "${@:-build}"
