#!/bin/sh
set -eu
cd "$(dirname "$0")"
: "${THERMAL_DEVICE_SERIAL:?请先设置THERMAL_DEVICE_SERIAL为自己的设备序列号}"
exec .venv/bin/python -u app.py
