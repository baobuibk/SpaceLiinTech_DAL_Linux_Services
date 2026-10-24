#!/usr/bin/env bash
set -eo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
sdk_root="${SAM_XBLD_SDK_ROOT:-/home/buildmachine/oecore-obcv110}"
sdk_environment="$sdk_root/environment-setup-cortexa7hf-neon-vfpv4-mchp-linux-gnueabi"

if [[ ! -r "$sdk_environment" ]]; then
    echo "OBC SDK environment not found: $sdk_environment" >&2
    exit 1
fi

# The Yocto setup script references optional variables that may be unset.
set +u
source "$sdk_environment"
set -u

if [[ ! -d "${SDKTARGETSYSROOT:-}" ]]; then
    echo "OBC SDK target sysroot is missing" >&2
    exit 1
fi

exec make -C "$project_dir" BUILD=build-obcv110 "$@"
