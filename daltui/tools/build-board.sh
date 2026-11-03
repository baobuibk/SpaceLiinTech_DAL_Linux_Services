#!/bin/sh
# Cross-build daltui for the DAL OBC (SAMA7G5, Cortex-A7 hard-float, glibc) with the Yocto SDK.
# Usage: tools/build-board.sh [SDK_DIR]      default SDK_DIR=/usr/local/oecore-x86_64
# Output: target/armv7-unknown-linux-gnueabihf/release/daltui
set -e
SDK=${1:-/usr/local/oecore-x86_64}
SYS=$SDK/sysroots/cortexa7hf-neon-vfpv4-mchp-linux-gnueabi
GCC=$SDK/sysroots/x86_64-mchpsdk-linux/usr/bin/arm-mchp-linux-gnueabi/arm-mchp-linux-gnueabi-gcc
[ -x "$GCC" ] || { echo "cross gcc not found: $GCC" >&2; exit 1; }
cd "$(dirname "$0")/.."
LINK=$(mktemp)
trap 'rm -f "$LINK"' EXIT
printf '#!/bin/sh\nexec %s -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard --sysroot=%s "$@"\n' "$GCC" "$SYS" > "$LINK"
chmod +x "$LINK"
rustup target list --installed | grep -qx armv7-unknown-linux-gnueabihf || rustup target add armv7-unknown-linux-gnueabihf
CARGO_TARGET_ARMV7_UNKNOWN_LINUX_GNUEABIHF_LINKER=$LINK cargo build --release --target armv7-unknown-linux-gnueabihf
file target/armv7-unknown-linux-gnueabihf/release/daltui
