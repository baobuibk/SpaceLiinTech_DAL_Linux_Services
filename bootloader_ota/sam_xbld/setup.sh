#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="${XBLD_BIN_DIR:-/home/root/tools/bin}"

echo "==============================="
echo "   samv71_xbld Setup"
echo "==============================="
echo "  1. Normal build"
echo "  2. Build + clean build files"
echo "  0. Exit"
echo "==============================="
read -r -p "Select: " choice

case "$choice" in
    0) exit 0 ;;
    1|2) ;;
    *) echo "[x] Invalid selection" >&2; exit 1 ;;
esac

make -C "$PROJECT_DIR"
mkdir -p "$BIN_DIR"
install -m 755 "$PROJECT_DIR/build/samv71_xbld" "$BIN_DIR/samv71_xbld"
echo "[v] Binary installed: $BIN_DIR/samv71_xbld"

if [ "$choice" = 2 ]; then
    make -C "$PROJECT_DIR" clean
    echo "[v] Build files cleaned; project source retained"
fi

echo "[v] Done"
