#!/usr/bin/env bash
set -euo pipefail
: "${SDKTARGETSYSROOT:?Build with ./build.sh target}"
exec arm-mchp-linux-gnueabi-gcc \
    -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
    --sysroot="$SDKTARGETSYSROOT" "$@"
