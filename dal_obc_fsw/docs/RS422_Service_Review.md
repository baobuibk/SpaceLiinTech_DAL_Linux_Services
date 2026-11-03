# RS-422 Service Review

## Scope and conclusion

This review covers the standalone DAL `rs422-service`, its systemd unit, and board integration. It is separate from the RS-422 TMTC implementation inside `dal_obc_fsw`. Several defects should be fixed before deployment, including a heap buffer overflow confirmed during the preceding host-side review.

## Service overview

The single-process serial server supports ping, download, upload, file status, listing, deletion, and abort. Uploads use parts of up to 512 bytes, with a maximum of 8192 parts (4 MiB), and part and whole-file CRC32 checks. Downloads read staged chunks from `/home/root/tmp/run/rs422-chunk`; uploads target `/home/bee`.

The service does not program FPGA flash or control FPGA reset.

## Findings

### 1. High: download response overflows its heap buffer

`send_download_response()` allocates and transmits `chunk_size + 7` bytes, but the frame requires `chunk_size + 8`: five header bytes, two part-number bytes, data, and the final carriage return. The terminator write overflows the allocation by one byte, and the terminator is omitted from transmission.

**Evidence:** AddressSanitizer reproduced a one-byte heap-buffer-overflow at line 400 with a three-byte payload.

**Recommendation:** Correct the allocation and transmission length. Test exact frame bytes and maximum-size downloads.

Source: `sources/meta-src-dalobcv110/recipes-app/rs422-service/files/rs422_service.c`, `send_download_response()`, lines 386–404.

### 2. High: upload assembly can destroy an existing valid file

Assembly opens the destination with `O_TRUNC` before verification. Assembly or CRC failure then deletes it, losing the previous file. Part acknowledgments precede assembly completion, and writes are not explicitly synced for power-loss durability.

**Recommendation:** Assemble into a temporary file in the destination directory, verify and sync it, atomically rename it into place, and sync the directory. Distinguish part acceptance from completed-file success.

Source: `sources/meta-src-dalobcv110/recipes-app/rs422-service/files/rs422_service.c`, `handle_upload()`, lines 697–760.

### 3. High: ordinary retries cannot recover failed assembly

After assembly failure, the bitmap still marks all parts as received. Retransmitted parts receive success acknowledgments but return without triggering assembly. File status can remain “in progress” indefinitely when the final file is absent. An explicit abort and restart is needed to clear the state.

**Recommendation:** Track assembly failure explicitly and permit assembly retry or replacement of affected parts. Return a failure status instead of indefinite progress.

Source: `sources/meta-src-dalobcv110/recipes-app/rs422-service/files/rs422_service.c`, duplicate handling at lines 649–655 and `handle_file_stat()` at lines 789–801.

### 4. High: path validation does not confine file access

`resolve_safe_path()` checks only the `/home/bee` string prefix. It accepts `/home/bee_outside/file.bin`, outside the intended directory. Symlinks are not confined either. The unit runs as root by default, increasing the consequences of upload and delete operations.

**Evidence:** A host-side helper test accepted that outside-prefix path. No file was written at that path.

**Recommendation:** Enforce a directory boundary and use directory-relative file operations with protection against symlink traversal. Run with only the required filesystem and serial-device privileges.

Source: `sources/meta-src-dalobcv110/recipes-app/rs422-service/files/rs422_service.c`, `resolve_safe_path()`, lines 280–306.

### 5. Integration: configured UART differs from DAL RS-422

The unit selects `/dev/ttyS4` at 921600 baud. The board mapping identifies this as PC104 RS232_2; DAL RS-422 is `/dev/ttyS1`, used by `dal_obc_fsw`. The DAL image recipe explicitly includes `dal-obc-fsw`; no explicit inclusion of `rs422-service` was found in the reviewed DAL configuration. Recipe availability alone does not establish runtime activation.

**Recommendation:** Decide which application owns the UART before enabling this service. Do not run both serial readers on `/dev/ttyS1`. Confirm package installation and protocol compatibility on the intended image.

Sources: `sources/meta-src-dalobcv110/recipes-app/rs422-service/files/rs422.service`, line 8; `sources/meta-src-dalobcv110/recipes-kernel/linux/linux-mchp/sama7g5ek-satellite-dev.dtsi`, lines 321–324; `sources/meta-src-dalobcv110/recipes-core/images/core-image-satellite.bb`, line 132.

### 6. Medium: serial failures may stop the service without restart

Receive errors terminate `rx_loop()`, but `main()` returns zero. The unit's `Restart=on-failure` therefore does not recover these exits. Response callers ignore transmit errors, and `write_all()` has no deadline.

**Recommendation:** Propagate fatal errors to a nonzero process exit, handle interrupted operations, and implement bounded writes with explicit error handling.

Source: `sources/meta-src-dalobcv110/recipes-app/rs422-service/files/rs422_service.c`, `write_all()` at lines 197–208, receive loop at lines 970–973, and exit handling at lines 1122–1126.

### 7. Medium: creation of upload directories is incomplete

`mkdirs()` omits the final directory component. Its caller passes the parent directory, so uploads requiring a new parent directory can fail while directory creation reports success.

**Evidence:** A host-side test returned success while the requested final directory remained absent.

**Recommendation:** Create the final component and verify that existing components are directories. Report the actual filesystem error instead of treating every part-file open failure as disk exhaustion.

Source: `sources/meta-src-dalobcv110/recipes-app/rs422-service/files/rs422_service.c`, `mkdirs()` at lines 310–325 and caller at lines 657–679.

## Additional protocol and recovery limitations

- Upload CRCs protect file data, but not command IDs, paths, or transfer metadata. Frame integrity should cover these fields too.
- Upload state exists only in RAM; restart loses the bitmap and prevents reliable resumption of existing part files.
- File listing is capped at 32 entries, truncates filenames to 31 characters, and has no implemented pagination cursor.

## Validation and follow-up

The preceding review used source inspection and a temporary native C harness that included the service source. AddressSanitizer confirmed the download overflow; helper tests confirmed the path-prefix and directory-creation defects. These tests did not access board UARTs or program hardware. The remaining findings are based on source inspection. Line numbers identify the reviewed source and may change with subsequent edits.

Fix the overflow, upload replacement/recovery behavior, and path confinement first. Then validate UART ownership and add regression coverage for exact response framing, assembly failures, duplicate retries, filesystem failures, serial disconnects, and restart recovery. Hardware validation remains outstanding.

Saving this review does not change the service implementation or `AGENTS.md`.
