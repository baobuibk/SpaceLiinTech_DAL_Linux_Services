#!/bin/sh
# Cross-build the Windows x64 terminal client on Linux (MinGW-w64 required).
set -eu
cd "$(dirname "$0")/.."
TARGET=x86_64-pc-windows-gnu
WINDOWS_CC=${CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER:-${WINDOWS_CC:-x86_64-w64-mingw32-gcc}}
if ! command -v "$WINDOWS_CC" >/dev/null 2>&1; then
    echo "Missing MinGW linker: $WINDOWS_CC" >&2
    echo "Install gcc-mingw-w64-x86-64, or set WINDOWS_CC to its executable." >&2
    exit 1
fi
WINDOWS_CC=$(command -v "$WINDOWS_CC")
# Rust also invokes dlltool when generating Windows import libraries.
PATH="$(dirname "$WINDOWS_CC"):$PATH"
export PATH
command -v x86_64-w64-mingw32-dlltool >/dev/null 2>&1 || { echo "Missing MinGW dlltool (binutils-mingw-w64-x86-64)" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "python3 is required to package the ZIP" >&2; exit 1; }
rustup target list --installed | grep -qx "$TARGET" || rustup target add "$TARGET"
CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER="$WINDOWS_CC" \
    cargo build --release --locked --target "$TARGET" --target-dir target
python3 tools/package-windows.py "target/$TARGET/release/daltui.exe"
