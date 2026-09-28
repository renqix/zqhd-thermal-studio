#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
arduino-cli compile --fqbn rp2040:rp2040:rpipico:freq=250,opt=Optimize3 --config-file .toolchain/arduino-cli.json --build-path .build --build-property "compiler.cpp.extra_flags=-ffile-prefix-map=$ROOT=/project -ffile-prefix-map=$HOME=/home" --build-property "compiler.c.extra_flags=-ffile-prefix-map=$ROOT=/project -ffile-prefix-map=$HOME=/home" firmware/thermal_v2
mkdir -p dist
cp .build/thermal_v2.ino.uf2 dist/zqhd-tiny-public.uf2
shasum -a 256 dist/zqhd-tiny-public.uf2
