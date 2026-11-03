#!/bin/bash
set -eo pipefail
BME_SDK_ROOT=${BME_SDK_ROOT:-/home/buildmachine/oecore-obcv110}
source "$BME_SDK_ROOT/environment-setup-cortexa7hf-neon-vfpv4-mchp-linux-gnueabi"
cd "$(dirname "${BASH_SOURCE[0]}")"
exec make "$@"
