#!/bin/bash
set -eo pipefail
HK_SDK_ROOT=${HK_SDK_ROOT:-/home/buildmachine/oecore-obcv110}
source "$HK_SDK_ROOT/environment-setup-cortexa7hf-neon-vfpv4-mchp-linux-gnueabi"
cd "$(dirname "${BASH_SOURCE[0]}")"
exec make "$@"
