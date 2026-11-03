#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
sdk_root=${SDK_ROOT:-/home/buildmachine/oecore-obcv110}
source "$sdk_root/environment-setup-cortexa7hf-neon-vfpv4-mchp-linux-gnueabi"

compiler_path=$(command -v "${TARGET_PREFIX}gcc")
compiler_prefix=${compiler_path%gcc}
export CFLAGS="${CFLAGS:-} --sysroot=$SDKTARGETSYSROOT -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard"
export LDFLAGS="${LDFLAGS:-} --sysroot=$SDKTARGETSYSROOT -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -Wl,--dynamic-linker=/usr/lib/ld-linux-armhf.so.3"

cd "$project_dir"
"${PYTHON:-python3}" ./waf configure --out=build-target --toolchain="$compiler_prefix"
"${PYTHON:-python3}" ./waf build_lite --out=build-target
