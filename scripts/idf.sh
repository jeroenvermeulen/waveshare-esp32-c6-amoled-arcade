#!/usr/bin/env bash
# Run idf.py with ESP-IDF loaded, e.g. `scripts/idf.sh build flash monitor`.
# Set IDF_PATH / IDF_PYTHON_ENV_PATH yourself if your install lives elsewhere,
# and ESPPORT to pick a serial port (default: the first USB modem port).
set -e
export IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"
if [ -z "$IDF_PYTHON_ENV_PATH" ]; then
    for d in "$HOME"/.espressif/python_env/idf5.5_py*_env; do
        [ -x "$d/bin/python" ] && export IDF_PYTHON_ENV_PATH="$d"
    done
fi
if [ -z "$ESPPORT" ]; then
    for p in /dev/cu.usbmodem* /dev/ttyACM*; do
        [ -e "$p" ] && export ESPPORT="$p" && break
    done
fi
. "$IDF_PATH/export.sh" > /dev/null
cd "$(dirname "$0")/.."
exec idf.py "$@"
