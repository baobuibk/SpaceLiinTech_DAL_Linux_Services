DALTUI for Windows x64 (terminal application)

Extract this entire ZIP to a writable folder. Open Windows Terminal or PowerShell
in that folder. Do not run the Linux/Pi binary on Windows.

Simulation (no hardware required):
  .\daltui.exe --sim

Physical RS-422 (replace COM3 with the adapter's port in Device Manager):
  .\daltui.exe --rs422 COM3 --baud 921600 --poll

COM10 and higher are supported. Serial settings: 8 data bits, no parity,
1 stop bit, no hardware/software flow control. The USB-RS422 driver must support
921600 baud. Close other programs using the same COM port. OBC FSW stays running.

--poll enables housekeeping; --manual reads on UPDATE; omit both for control-only.
The Windows client supports simulation and physical COM ports. --console and
--compare use Linux Unix sockets and are not available on Windows.

Use a terminal at least 120x40 (120x46 for the IR Camera page). Exit with q.
No TERM environment variable is required on Windows.

OBC > Data transfer saves to .\downloads under the current working directory.
To choose a destination in PowerShell:
  $env:DAL_TUI_DOWNLOAD_DIR = "$env:USERPROFILE\Downloads\DAL"
  .\daltui.exe --rs422 COM3 --baud 921600 --poll
Use a local NTFS destination: verified downloads are published using a hard link.
Wait for "DONE: saved, CRC32 verified"; failed/incomplete files retain .part.
Use only one file-transfer client at a time.

File types: HK (0) housekeeping, SCI1 (1) images, SCI2 (2) LWL/PC logs.
Update OBC FSW to the matching ICD v1.2 build (dat_sel uses a U32 file index).
This Windows client does not require changes to EXP firmware.

Build validation is performed by cross-compilation and automated tests; physical
Windows USB-RS422 acceptance must be checked with the actual adapter and OBC.

PDU status requires the matching OBC FSW with the 16-byte PDU trailer
(pwr_stat all payload 184 bytes). Temperature, mode and TPS state update over COM.
TRANS OFF is NOT POWERED, not a communication fault. Invalid values show N/A.

This build uses the ICD v1.2 native payload lengths and separate DAL job/query
extensions. Update dal_obc_fsw on the OBC from the matching deploy package.

EXP IR Camera point measurement requires EXP firmware build 457 or later.
Edit Center X, Center Y and Delta, then Measure temperature. The fresh SRAM-only
read returns center/max/average and creates no PSRAM image. Capture and point
requests can run while monitoring remains enabled.

EXP build 458 or later: monitor samples each camera every 10 s. Stop cancels
the active monitor read at driver boundaries and preserves the previous sample.
