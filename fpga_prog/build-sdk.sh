#!/bin/bash
set -eo pipefail
FPGA_SDK_ROOT=${FPGA_SDK_ROOT:-/home/buildmachine/oecore-obcv110}
source "$FPGA_SDK_ROOT/environment-setup-cortexa7hf-neon-vfpv4-mchp-linux-gnueabi"
cd "$(dirname "${BASH_SOURCE[0]}")"
exec make "$@"
