#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
mode="${1:-host}"
if [[ $# -gt 1 || ( "$mode" != host && "$mode" != target ) ]]; then
    echo "Usage: $0 [host|target]" >&2
    exit 2
fi
cd "$project_dir"
gui_cargo="$(command -v cargo)"
export CARGO_TARGET_DIR="$project_dir/target"
if [[ "$mode" == host ]]; then
    "$gui_cargo" build --locked --release --bin exp_test_gui
    exit
fi

sdk_root="${EXP_TEST_GUI_SDK_ROOT:-${EXP_GUI_SDK_ROOT:-/home/buildmachine/oecore-obcv110}}"
sdk_environment="$sdk_root/environment-setup-cortexa7hf-neon-vfpv4-mchp-linux-gnueabi"
rust_target=armv7-unknown-linux-gnueabihf
if [[ ! -r "$sdk_environment" ]]; then
    echo "OBC SDK environment not found: $sdk_environment" >&2
    exit 1
fi
rust_libdir="$(rustc --print target-libdir --target "$rust_target")"
if ! compgen -G "$rust_libdir/libstd-*.rlib" >/dev/null; then
    echo "Install Rust target: rustup target add $rust_target" >&2
    exit 1
fi
# Yocto's environment script references optional variables.
set +u
source "$sdk_environment"
set -u
if [[ ! -d "${SDKTARGETSYSROOT:-}" ]]; then
    echo "SDK target sysroot missing: ${SDKTARGETSYSROOT:-unset}" >&2
    exit 1
fi
export CARGO_TARGET_ARMV7_UNKNOWN_LINUX_GNUEABIHF_LINKER="$project_dir/scripts/yocto-linker.sh"
export PKG_CONFIG_ALLOW_CROSS=1
unset CARGO_ENCODED_RUSTFLAGS
export RUSTFLAGS="-C target-cpu=cortex-a7"
"$gui_cargo" build --locked --release --target "$rust_target" --bin exp_test_gui
