#!/usr/bin/env bash
# Build the terminal application for the host or the OBC's Yocto Linux target.
set -euo pipefail
pdu_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
mode="${1:-host}"
if [[ $# -gt 1 ]]; then
    echo "Usage: $0 [host|target|--help]" >&2
    exit 2
fi
case "$mode" in
    -h|--help)
        cat <<'HELP'
Usage: ./build.sh [host|target]
  host     Native terminal release build (default).
  target   ARMv7 hard-float release build using the OBC Yocto SDK.

SDK defaults to /home/buildmachine/oecore-obcv110.
Override with PDU_SDK_ROOT=/path/to/sdk ./build.sh target.
Install Rust target once: rustup target add armv7-unknown-linux-gnueabihf
No deployment or changes to the Yocto image are performed.
HELP
        exit 0 ;;
    host|target) ;;
    *) echo "Unknown build mode: $mode (use --help)" >&2; exit 2 ;;
esac
cd "$pdu_dir"
pdu_cargo="$(command -v cargo)" || { echo "cargo is not on PATH" >&2; exit 1; }
# Keep output locations predictable even when called from another workspace.
export CARGO_TARGET_DIR="$pdu_dir/target"
if [[ "$mode" == host ]]; then
    "$pdu_cargo" build --locked --release --bin dal-pdu-gui --no-default-features
    echo "Built: $CARGO_TARGET_DIR/release/dal-pdu-gui"
    exit 0
fi
pdu_sdk="${PDU_SDK_ROOT:-/home/buildmachine/oecore-obcv110}"
pdu_environment="$pdu_sdk/environment-setup-cortexa7hf-neon-vfpv4-mchp-linux-gnueabi"
if [[ ! -r "$pdu_environment" ]]; then
    echo "Yocto SDK environment not found: $pdu_environment" >&2
    echo "Set PDU_SDK_ROOT to the installed OBC SDK directory." >&2
    exit 1
fi
pdu_target=armv7-unknown-linux-gnueabihf
pdu_rust_libdir="$(rustc --print target-libdir --target "$pdu_target")"
if ! compgen -G "$pdu_rust_libdir/libstd-*.rlib" >/dev/null; then
    echo "Missing Rust target. Run: rustup target add $pdu_target" >&2
    exit 1
fi
# Vendor SDK scripts may reference optional unset variables.
set +u
source "$pdu_environment"
set -u
if [[ ! -d "${SDKTARGETSYSROOT:-}" ]]; then
    echo "SDK target sysroot is missing: ${SDKTARGETSYSROOT:-unset}" >&2
    exit 1
fi
command -v arm-mchp-linux-gnueabi-gcc >/dev/null || {
    echo "SDK compiler arm-mchp-linux-gnueabi-gcc is missing" >&2; exit 1;
}
export CARGO_TARGET_ARMV7_UNKNOWN_LINUX_GNUEABIHF_LINKER="$pdu_dir/scripts/yocto-linker.sh"
# Apply the board CPU to target crates; Cargo builds host tools separately.
unset CARGO_ENCODED_RUSTFLAGS
export RUSTFLAGS="-C target-cpu=cortex-a7"
echo "Building $pdu_target with sysroot $SDKTARGETSYSROOT"
"$pdu_cargo" build --locked --release --target "$pdu_target" --bin dal-pdu-gui --no-default-features
echo "Built: $CARGO_TARGET_DIR/$pdu_target/release/dal-pdu-gui"
