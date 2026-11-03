#!/bin/bash
set -euo pipefail
SDK_ROOT=${SDK_ROOT:-/home/buildmachine/oecore-obcv110}
source "$SDK_ROOT/environment-setup-cortexa7hf-neon-vfpv4-mchp-linux-gnueabi"
cd "$(dirname "${BASH_SOURCE[0]}")"
exec make sdk-build
