#!/usr/bin/env bash
# Cargo expects a linker executable, whereas Yocto's CC contains arguments.
set -euo pipefail
: "${SDKTARGETSYSROOT:?Run ./build.sh target to load the Yocto SDK environment}"
exec arm-mchp-linux-gnueabi-gcc \
    -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
    --sysroot="$SDKTARGETSYSROOT" "$@"
