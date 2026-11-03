#!/bin/sh
# SPDX-License-Identifier: MIT
# DAL OBC v1.1.0: Q7 inverts PD0; brightness 1 asserts PROGRAM_B.
set -eu

FLASH=/home/root/tools/bin/spi_flash_s25
LEDS=/sys/class/leds
LOCK=/run/lock/dal-fpga-prog.lock
CONF_DIR=/home/bee/fpga
CATALOG=/run/fpga-program.list
export LC_ALL=C

# Named process exit codes (POSIX shells expose only 0..255 through $?).
# Logical -1 = 255: pre-write flash ID/access validation failed; no write started.
# Logical -2 = 254: readback/compare failed after writing, including a mismatch
# reported by spi_flash_s25 during its built-in write verification.
# 0 means flash verified and FPGA reset released, not confirmed FPGA readiness.
readonly RC_SUCCESS=0
readonly RC_ID_FAILED=255
readonly RC_VERIFY_FAILED=254
readonly RC_OPERATION_FAILED=1
readonly RC_INVALID_ARGUMENT=2

usage() {
    echo 'Usage: fpga_program check'
    echo '       fpga_program flash INDEX [--power-cycle]'
    echo '       fpga_program --bin PATH [--power-cycle]'
    echo 'check lists /home/bee/fpga/*.bin; flash uses the most recent list.'
    echo 'Write a raw FPGA SPI boot binary at address 0 and verify it.'
    echo '--power-cycle: turn FPGA power OFF for 1 second before turning it ON.'
    echo 'Interrupts FPGA operation. Success does not confirm FPGA readiness.'
}

fail_code() {
    code=$1
    shift
    echo "fpga_program: $*" >&2
    exit "$code"
}
fail() { fail_code "$RC_OPERATION_FAILED" "$@"; }
invalid() { echo "fpga_program: $*" >&2; usage >&2; exit "$RC_INVALID_ARGUMENT"; }

# Store paths and hashes in pairs; never evaluate catalog contents as shell.
check_files() (
    umask 077
    temp=$(mktemp "${CATALOG}.XXXXXX") || fail 'cannot create file list'
    trap 'rm -f -- "$temp"' EXIT
    trap 'exit 1' HUP INT TERM
    # Invalidate an old list even if this scan fails.
    rm -f -- "$CATALOG"
    [ -d "$CONF_DIR" ] || fail "directory not found: $CONF_DIR"
    count=0
    for file in "$CONF_DIR"/*.bin; do
        [ -f "$file" ] && [ -r "$file" ] || continue
        case "$file" in
            *'
'*) fail 'file names containing newlines are unsupported' ;;
        esac
        digest=$(sha256sum < "$file") || fail "cannot checksum $file"
        printf '%s\n%s\n' "${digest%% *}" "$file" >> "$temp"
        count=$((count + 1))
        printf '[%s]: %s\n' "$count" "${file##*/}"
    done
    mv -f -- "$temp" "$CATALOG"
    [ "$count" -gt 0 ] || echo "No .bin files found in $CONF_DIR."
)

bin=
cycle=0
mode=direct
index=
expected_hash=
case "${1-}" in
    check)
        [ "$#" -eq 1 ] || invalid 'check takes no arguments'
        mode=check
        shift
        ;;
    flash)
        [ "$#" -ge 2 ] || invalid 'flash needs an index from check'
        index=$2
        case "$index" in ''|*[!0-9]*|0*) invalid 'index must be a positive decimal without leading zeros' ;; esac
        mode=flash
        shift 2
        ;;
esac
while [ "$#" -gt 0 ]; do
    case "$1" in
        --bin)
            [ "$mode" = direct ] || invalid '--bin cannot be combined with flash'
            [ "$#" -ge 2 ] || invalid '--bin needs a path'
            [ -z "$bin" ] || invalid 'duplicate --bin'
            bin=$2
            shift 2
            ;;
        --power-cycle) cycle=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) invalid "unknown argument: $1" ;;
    esac
done
[ "$mode" != direct ] || [ -n "$bin" ] || invalid '--bin is required'
[ "$(id -u)" -eq 0 ] || fail 'run as root'

# Serialize list replacement and selection with the entire deployment.
# Also coordinates with the standalone service/fpga_prog programmer.
exec 9>"$LOCK"
flock -n 9 || fail 'another FPGA deployment is running'
if [ "$mode" = check ]; then
    check_files
    exit 0
fi
if [ "$mode" = flash ]; then
    [ -f "$CATALOG" ] || invalid 'run fpga_program check first'
    count=0
    while IFS= read -r digest && IFS= read -r file; do
        count=$((count + 1))
        if [ "$count" = "$index" ]; then
            bin=$file
            expected_hash=$digest
            break
        fi
    done < "$CATALOG"
    [ -n "$bin" ] || invalid 'index not in saved list; run fpga_program check'
    printf 'Selected [%s]: %s\n' "$index" "$bin"
fi
[ -f "$bin" ] && [ -r "$bin" ] || invalid 'input must be a readable regular file'
size=$(stat -c %s -- "$bin") || invalid 'cannot stat input'
[ "$size" -gt 0 ] && [ "$size" -le 16777216 ] || invalid 'input must be 1..16777216 bytes'
[ -x "$FLASH" ] || fail "missing $FLASH"

gpio_read() {
    value=$(cat "$LEDS/$1/brightness") || return 1
    case "$value" in 0|1) printf '%s\n' "$value" ;; *) return 1 ;; esac
}
gpio_set() {
    printf '%s\n' "$2" > "$LEDS/$1/brightness" || return 1
    [ "$(gpio_read "$1")" = "$2" ]
}

snapshot=
started=0
complete=0
cleanup() {
    result=$?
    trap - EXIT HUP INT TERM
    if [ "$started" -eq 1 ] && [ "$complete" -eq 0 ]; then
        # Never release reset or switch to FPGA after an unsuccessful update.
        gpio_set fpga_program_b 1 || echo 'Cannot confirm FPGA reset; inspect GPIO.' >&2
        echo 'Deployment failed; FPGA reset requested. Flash/power routing retained; inspect before retry.' >&2
        [ "$result" -ne "$RC_SUCCESS" ] || result=$RC_OPERATION_FAILED
    fi
    [ -z "$snapshot" ] || rm -f -- "$snapshot"
    exit "$result"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

# Use one private snapshot for write and verify even if the source is replaced.
umask 077
snapshot=$(mktemp /tmp/fpga-program.XXXXXX) || fail 'cannot create input snapshot'
cp -- "$bin" "$snapshot" || fail 'cannot copy input'
size=$(stat -c %s "$snapshot") || fail 'cannot stat snapshot'
[ "$size" -gt 0 ] && [ "$size" -le 16777216 ] || invalid 'snapshot must be 1..16777216 bytes'
if [ "$mode" = flash ]; then
    digest=$(sha256sum < "$snapshot") || fail 'cannot checksum snapshot'
    [ "${digest%% *}" = "$expected_hash" ] || invalid 'file changed since check; run fpga_program check again'
fi
for pin in fpga_program_b pow_onoff_fpga sel_flash_spi_fpga; do
    [ -w "$LEDS/$pin/brightness" ] && gpio_read "$pin" >/dev/null || fail "GPIO unavailable: $pin"
done

started=1
gpio_set fpga_program_b 1 || fail 'cannot assert FPGA reset'
sleep 1
"$FLASH" --sw mpu || fail 'cannot select MPU'
if [ "$cycle" -eq 1 ]; then
    gpio_set pow_onoff_fpga 0 || fail 'cannot turn FPGA power OFF'
    sleep 1
fi
gpio_set pow_onoff_fpga 1 || fail 'cannot turn FPGA power ON'
sleep 1
# id validates the supported JEDEC ID and flash status before any write.
"$FLASH" id || fail_code "$RC_ID_FAILED" 'flash identification failed (-1)'
echo "Programming $size bytes at flash address 0..."
if "$FLASH" write -a 0 -f "$snapshot" --yes; then
    :
else
    write_result=$?
    if [ "$write_result" -eq "$RC_VERIFY_FAILED" ]; then
        fail_code "$RC_VERIFY_FAILED" 'flash write readback mismatch (-2)'
    fi
    fail 'flash write failed'
fi
# verify reads the entire programmed range and compares every byte against
# the exact private snapshot passed to write; do not hand off on failure.
"$FLASH" verify -a 0 -f "$snapshot" ||
    fail_code "$RC_VERIFY_FAILED" 'flash readback/verification failed (-2)'
"$FLASH" --sw fpga || fail 'cannot return flash to FPGA'
gpio_set fpga_program_b 0 || fail 'cannot release FPGA reset'
complete=1
echo 'DONE: flash verified; FPGA configuration triggered. Check FPGA version/heartbeat separately.'

exit "$RC_SUCCESS"
