# Repository Guidelines

## Project Structure & Module Organization

This directory contains the DAL OBC flight software and its local console client. `src/main.c` starts the application; `src/services/` implements telemetry, power, UART, GUI, and housekeeping services. Public headers live in `include/`, with matching service headers in `include/services/`. Shared protocol, file, and utility code lives under `lib/`. `tests/` contains C unit tests, Python integration tests, and the `tests/fsw_auto/` RS-422 simulation suite. `tool/pc_client/` is the PC client; `files/` holds the systemd unit. Make writes host and ARM binaries to `build-host/` and `build-target/`.

## Build, Test, and Development Commands

Run these from this directory:

- `make`: build the host flight software and console client.
- `make check`: build and run C tests, Python integration tests, and binary help checks.
- `make fsw-sim-test`: run the longer Python standard-library simulation suite. Use `FSW_TEST_ARGS="-k framing"` to select cases.
- `make target`: cross-compile with the ARM SDK; override its location with `SDK_ROOT=/path/to/sdk`.
- `make deploy`: stage ARM binaries and `files/dal_obc_fsw.service` under `../deploy/dal_obc_fsw/`.

See `README.md` for board installation and runtime configuration.

## Coding Style & Naming Conventions

Follow the surrounding C style: four-space indentation, `snake_case` functions and files, and service interfaces in matching `include/services/*.h` and `src/services/*.c` files. Keep wire formats and byte order explicit; protocol compatibility matters here. Python uses four spaces and `snake_case` names. The Makefile compiles C11 with `-Wall -Wextra`; no repository formatter or linter is configured.

## Testing Guidelines

Add focused `tests/test_*.c` or `tests/test_*.py` coverage for changed behavior, and register new C tests in the Makefile. Simulation cases belong in `tests/fsw_auto/cases/test_*.py` and use `unittest`; run `make check` and, for protocol or console changes, `make fsw-sim-test`. No numeric coverage threshold is specified. Keep simulated file operations in the fixture's temporary data directory; see `tests/fsw_auto/README.md` before adding destructive or clock-related cases.

## Commit & Pull Request Guidelines

Git history currently contains only `first commit`, so there is no established message convention. Use a short imperative subject that names the changed service or behavior. In pull requests, summarize the change, note affected wire or runtime behavior, list commands run, and link the relevant issue or design document when one exists. Include screenshots only for visible GUI changes.
