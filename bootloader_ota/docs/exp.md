# Flash the EXP (SAMV71)

The EXP application listens for a plain text `reset` command on MCU UART0. The `samv71_xbld` client sends that command, waits for the SAMV71 bootloader, then uses its binary xTP/XCLI protocol to erase, write, and verify flash. The full `flash` operation jumps to the application after updating the main slot.

## 1. Prepare a bootloader image

Use an EXP Intel HEX image packaged with the firmware project's `tools/xbld_sign.py`. Its xBLD header contains the target slot and image CRC. A raw application HEX without the header is not suitable for this client.

From the EXP firmware project, package the same application build separately for the main and backup slots:

```sh
python3 tools/xbld_sign.py path/to/application.hex --slot 0
python3 tools/xbld_sign.py path/to/application.hex --slot 1
```

Slot 0 has its header at `0x00420000`; slot 1 has its header at `0x00520000`. The signer prints each generated image path and normally gives the backup image an `_backup.hex` suffix. Keep both files for transfer. Do not copy the slot 0 HEX and relabel it as a slot 1 image: the image's flash addresses must match its target slot. The signer requires Python `intelhex`; see the EXP firmware project's build instructions for the application build itself.

## 2. Build and transfer the client and image

On the Linux build host, from `service/bootloader_ota/sam_xbld`:

```sh
make check
make deploy
```

`make deploy` uses the OBC Yocto SDK under `~/oecore-obcv110` and stages the ARM executable at `service/deploy/samv71_xbld/samv71_xbld`. Create the image directory on the OBC:

```sh
mkdir -p /home/bee/firmware/EXP
```

From the Linux build host's `Linux_DAL` directory, set the OBC host name and the two paths printed by the signer, then copy the files:

```sh
OBC_HOST=dal-obc
EXP_MAIN_HEX=/path/to/main-image.hex
EXP_BACKUP_HEX=/path/to/backup-image.hex
scp service/deploy/samv71_xbld/samv71_xbld "root@${OBC_HOST}:/home/bee/samv71_xbld"
scp "$EXP_MAIN_HEX" "$EXP_BACKUP_HEX" "root@${OBC_HOST}:/home/bee/firmware/EXP/"
```

On the OBC, run `chmod 755 /home/bee/samv71_xbld` if the copied executable has lost its execute bit.

Use an absolute image path when it is stored outside `/home/bee/firmware/EXP`.

## 3. Check the connection and select an image

On the OBC, with the EXP UART0 connected to `/dev/ttyS4`:

```sh
/home/bee/samv71_xbld --port /dev/ttyS4 --baud 250000
```

The prompt is `xbld>`. Enter:

```text
xbld> check
xbld> ping
```

`check` lists numbered packaged images from `/home/bee/firmware/EXP` and marks backup images ending in `_backup.hex`, `_s1.hex`, or `_slot1_signed.hex` as `[BACKUP slot 1]`. Confirm that both images show `[OK]` for the CRC, the expected hardware ID, and the intended slot. `ping` reports whether the application text CLI or bootloader responds. If the EXP is wired to another OBC UART, change `--port`. Change `--baud` only when the application and bootloader builds use that same rate.

## 4. Flash and verify

At the prompt, use the numbers shown by `check`. For example, if the listing shows the main image as entry 1 and the backup image as entry 2, flash the backup first and then the main image:

```text
xbld> flash 2
xbld> flash 1
```

These numbers are **catalog entries, not slot numbers**. Check the actual listing before using them. `flash` sends the text reset, pings the bootloader, erases the selected slot, writes the packaged image, and verifies its CRC. Flashing slot 1 leaves the backup staged without jumping; flashing slot 0 jumps to the updated main application. The client prints the selected slot before erasing. `flash latest` selects one image from the catalog and does not update both slots.

To update both slots with one-shot commands, use the two separately packaged files and explicit slot options:

```sh
EXP_BACKUP_HEX=/home/bee/firmware/EXP/backup-image_backup.hex
EXP_MAIN_HEX=/home/bee/firmware/EXP/main-image.hex
/home/bee/samv71_xbld --port /dev/ttyS4 --baud 250000 --slot 1 --flash "$EXP_BACKUP_HEX"
/home/bee/samv71_xbld --port /dev/ttyS4 --baud 250000 --slot 0 --flash "$EXP_MAIN_HEX"
```

Set `EXP_BACKUP_HEX` and `EXP_MAIN_HEX` to the actual transferred paths. Match `--slot 1` only with the slot 1 image and `--slot 0` only with the slot 0 image. For `--flash latest`, the client scans `/home/bee/firmware/EXP` and uses the image's catalog slot unless `--slot` is supplied. A bare filename in the SAM client's one-shot mode is relative to the current working directory; use an absolute path for a specific file.

## Manual steps and checks

Use these menu commands only when performing the update in separate steps. First update slot 1 with the backup image number from `check`:

```text
xbld> reboot
xbld> erase backup
xbld> write 2 backup
xbld> verify backup
```

Then update slot 0 with the main image number:

```text
xbld> erase
xbld> write 1
xbld> verify
xbld> jump
```

The `1` and `2` values above assume the example catalog order; run `check` first and substitute its actual entry numbers. With the current client, standalone `erase` and `write` probe the bootloader and request entry if the application has resumed. `write` does not automatically erase, verify, or jump. If the bootloader reset between `erase` and `write`, the client may ask whether to erase again because its in-memory state was cleared. The menu also accepts `status`, `bootinfo`, `xbldinfo`, and `help`; `help` lists the startup arguments. `xbldinfo` shows the last host-side write or flash attempt.

If the bootloader does not answer after `reset`, confirm the UART device, serial wiring, and baud in both MCU builds. Do not rely on a successful `check` as a link test: it only reads files on the OBC.

If `write` reports `[xcli] send failed rc=1`, the xTP ARQ command did not receive an acknowledgment. The bootloader retries autoboot after 16 seconds of idle time, so it may have returned to the application. The current client automatically requests bootloader entry for standalone `erase` and `write`. With an older client, run `reboot` and wait for `[OK] Bootloader responding` before `erase backup` or `write <backup-image-number> backup`. If `reboot` also fails, check the port, baud, wiring, and installed MCU firmware before attempting another erase.
