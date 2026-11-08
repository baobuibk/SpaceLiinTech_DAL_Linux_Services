# DALTUI on Windows Terminal

The Windows x64 client supports **simulation** and **physical RS-422 through a COM port**. It uses the same ICD frames and controls as the Linux client. The OBC continues to run `dal_obc_fsw`; no EXP firmware change is needed for Windows support.

## Use the packaged executable

Build output on the Linux build machine:

```text
service/daltui/target/windows-x64/daltui-windows-x64.zip
```

Extract the ZIP, open Windows Terminal with a PowerShell profile, and enter the extracted folder. Use a terminal at least 120×40 cells (120×46 for IR Camera).

```powershell
cd "$env:USERPROFILE\Downloads\daltui-windows-x64"
.\daltui.exe --sim
```

For hardware, check the USB–RS422 adapter's COM number in Device Manager, close other programs using it, then run:

```powershell
.\daltui.exe --rs422 COM3 --baud 921600 --poll
```

Replace `COM3` with the actual port. `COM10` and higher are supported, as is the device-path form `\\.\COM10`. Serial settings are **921600 baud, 8N1, no flow control**. The adapter driver must support the chosen baud rate. The default port when omitted is `COM1`; specifying the port explicitly is recommended.

`--poll` enables periodic housekeeping; use `--manual` for UPDATE on request, or omit both for control-only operation. Press `q` to exit. Windows does not need `TERM=xterm-256color`.

`--console` and `--compare` require Linux Unix sockets and are rejected on Windows. A path such as `/run/dal/dal_obc_fsw/rs422.sock` is local to the OBC, not a COM port on Windows. Controls that require the local console remain unavailable over RS-422, just as on the Linux RS-422 client.

## Save OBC files on Windows

Choose a writable destination before opening DALTUI:

```powershell
$env:DAL_TUI_DOWNLOAD_DIR = "$env:USERPROFILE\Downloads\DAL"
.\daltui.exe --rs422 COM3 --baud 921600 --poll
```

Without this variable, the destination is `downloads` under the current working directory. Use a **local NTFS folder**: the existing transfer implementation publishes a verified file using a hard link, so a filesystem that does not support hard links will leave the transfer incomplete.

Open **OBC → Data transfer**, then List → Select → Download. Wait for **DONE: saved, CRC32 verified**; inspect **Local file** for the actual path. Partial/failed downloads retain `.part`. Types are HK (0) housekeeping, SCI1 (1) images, and SCI2 (2) LWL/PC logs. Only one client may control the OBC file selection at a time. See [the collection workflow](obc-file-collection.md).

## Cross-build on Linux

Prerequisites: Rust/Cargo with rustup, Python 3 (packaging only), and MinGW-w64 x64 GCC/binutils. On Debian/Ubuntu, install the MinGW packages through your normal system administration process:

```sh
sudo apt-get install gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64
```

From `Linux_DAL`:

```sh
make -C service daltui-windows
```

Or directly:

```sh
cd service/daltui
tools/build-windows.sh
```

The script adds the Rust `x86_64-pc-windows-gnu` target if needed, runs a locked release build, and packages the executable with a README and SHA256 checksums. This optional target is separate from the normal Linux OBC `make deploy`.

For a compiler outside PATH:

```sh
WINDOWS_CC=/path/to/mingw/bin/x86_64-w64-mingw32-gcc tools/build-windows.sh
```

MinGW `x86_64-w64-mingw32-dlltool` must also be available, normally beside the compiler. `CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER` takes precedence over `WINDOWS_CC`.

Outputs:

```text
target/x86_64-pc-windows-gnu/release/daltui.exe
target/windows-x64/daltui-windows-x64/daltui.exe
target/windows-x64/daltui-windows-x64.zip
```

## Build natively in Windows PowerShell

Install Rust with the MSVC toolchain and Visual Studio C++ Build Tools/Windows SDK. From the DALTUI source folder:

```powershell
.\tools\build-windows.ps1
```

The script builds `x86_64-pc-windows-msvc` and creates the same `target\windows-x64\daltui-windows-x64.zip` package. If local PowerShell policy blocks scripts, build the executable directly without running a script:

```powershell
rustup target add x86_64-pc-windows-msvc
cargo build --release --locked --target x86_64-pc-windows-msvc
.\target\x86_64-pc-windows-msvc\release\daltui.exe --sim
```

## Validation and limits

The GNU Windows release and Windows test executable are cross-compiled on Linux. Linux unit tests and a headless UI render exercise the shared code. The release executable is checked as a Windows x64 console PE and inspected for DLL imports. Native PowerShell/MSVC build execution and physical Windows USB–RS422 behavior still need verification on a Windows machine with the actual adapter and OBC.

The Windows COM implementation uses the [serialport builder](https://docs.rs/serialport/4.10.1/serialport/struct.SerialPortBuilder.html) with explicit data bits, parity, stop bits, flow control and timeout. Linux retains its existing Unix-socket and serial paths.
