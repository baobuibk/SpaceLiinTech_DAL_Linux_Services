#!/bin/bash
set -eo pipefail
SPI_EEPROM_SDK_ROOT=${SPI_EEPROM_SDK_ROOT:-/home/buildmachine/oecore-obcv110}
source "$SPI_EEPROM_SDK_ROOT/environment-setup-cortexa7hf-neon-vfpv4-mchp-linux-gnueabi"
cd "$(dirname "${BASH_SOURCE[0]}")"
exec make "$@"
