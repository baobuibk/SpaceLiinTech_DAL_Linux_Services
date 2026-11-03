# DAL OBC flight software documentation

This local site brings together the flight software architecture, host control
paths, command register, revision 1.0 ICD, and ICD-to-console mapping. The
[architecture and ICD coverage](flight-software-architecture.md) page compares
all 43 revision 1.0 command rows with the current dispatcher.

## Operate the OBC

- [Install deployed services](deployment.md) on the OBC.
- [Use `dal` commands](dal-commands.md) to discover and read OBC housekeeping
  values. The current command-line client covers the **OBC only**.
- [Use the flight software](flight-software.md) through the local console or
  binary RS422 link, including telemetry and file transfer commands.
- [Xem log flight software](flight-software-logs.md) trên board bằng
  `journalctl` và chọn mức chi tiết debug.
- [Run DALTUI](daltui-running.md) with the host simulator, on the OBC, or from
  an external computer over RS-422.
- [Find a flight command's RS-422 frame](flight-command-register.md) in the
  current console-to-binary command register.
- [Map OBC GUI actions to RS-422](exp-gui-command-map.md), including the EXP
  device command catalog.

## Hardware references

- [BEE-DAL ICD revision 1.0](icd.md): editable RS-422 frame, command, telemetry,
  and file transfer tables with noted source discrepancies.
- [BEE-DAL payload ICD and console mapping](icd-console-mapping.md): every ICD
  command paired with its current flight console command or marked missing.
- [MPU and FPGA interface and programming](DAL_OBC_V110_MPU_FPGA_Design_and_Programming.md)
- [RS-422 service review](RS422_Service_Review.md)

## Build this site

From `service/dal_obc_fsw`:

```sh
python3 -m pip install 'mkdocs>=1.6,<2'
mkdocs serve -f mkdocs.yml
```

Use `mkdocs build -f mkdocs.yml` to generate static HTML in `build-mkdocs/`.
The site uses MkDocs' Read the Docs theme. The ICD PDF is bundled under
`docs/assets/icd/`.
