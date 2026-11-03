# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

See also `README.md` (tabs, keys, simulated behaviour) and `AGENTS.md` (style, commit/PR conventions).

## What this is

`daltui` is a Rust 2021 terminal UI (ratatui 0.29 + crossterm 0.28) for the DAL OBC, EXP and PDU subsystems. It lives in `service/` of the parent Yocto tree, which is gitignored there, so this directory is untracked by the parent repo. **It has three data sources**: the simulator (`src/sim.rs`), the flight console text commands (default), and binary RS-422 frames (for protocol checks). The source is chosen by a flag or on the CFG tab.

## Commands

- Build: `cargo build` or `cargo build --release` (add `--offline` to use the local cargo cache). Release uses `opt-level = "z"`, LTO and strip.
- Run: `cargo run` (needs a terminal of at least 100x35). `--sim`, `--console [PATH]`, `--rs422 [PATH]` pick the source; `--compare` checks console against RS-422.
- Hardware-free live testing: `python3 tools/fsw_rig.py` (host `dal_obc_fsw` + scripted EXP/PDU in a sandbox), then `cargo run -- --console <sock> --dump obc`.
- Headless render, no TTY needed: `cargo run -- --dump exp 120x40 down,enter` (`--dump TAB [WxH] [KEYS]`, keys comma-separated). This is the main way to check layout and key handling without an interactive session.
- Checks: `cargo fmt --check`, `cargo clippy --all-targets`, `cargo test`. `cargo test` covers the CRC, framing, JSON reader and payload decoders (captured vectors); add `#[cfg(test)]` tests next to the logic you change.

## Architecture

The UI is data-driven. Three pieces have to agree, and adding a control touches the first two:

1. **`src/controls.rs`** holds static `Control` tables per subsystem and page (`Sub::controls(page)`, `Sub::pages()`). Each control has a stable, subsystem-prefixed `id` (e.g. `exp.motor`) and a `Kind` (`Toggle`, `Step`, `Choice`, `Action`, `Danger`). `Danger` controls require a `Y` confirmation.
2. **`src/sim.rs`** is the simulator and the model. `Sim` owns `Obc`, `Exp` and `Pdu` state shaped like the real telemetry (OBC housekeeping table, EXP PAC/NTC/BME, PDU Modbus map), which `src/live.rs` fills from real replies. It interprets controls by id string through `value(id)`, `set_bool`, `set_num`, `set_idx` and `run(id)`, and `tick(dt)` advances the physics. The firmware interlocks (rail ownership, pump/motor prerequisites, heater trip at 60 °C, PDU fault latch) are enforced here, and refusals go to the log.
3. **`src/app.rs`** holds `State` (current tab, page, selection, pending confirm, log) and all key handling in `on_key`. **`src/ui/`** only draws (`home`, `panels`, `controls`, `log`, shared chrome in `mod.rs`), and the renderers are generic over the control tables.

Live mode (`Mode::Console` / `Mode::Rs422`) adds four pieces: `src/proto.rs` (one `Cmd` per request with its console text *and* binary `(type, addr, payload)`, plus decoders), `src/link.rs` (transports and a worker thread; the UI only queues `Job`s and drains `Done`s), `src/live.rs` (`plan()` maps a control id to commands, `decode()` writes replies into the same `Obc`/`Exp`/`Pdu` fields; `unwired()` lists controls with no flight command, which the UI shows as `[ n/a ]`) and `src/ui/cfg.rs` (source selection and the command history). Polling follows `Live::auto`: AUTO sends `poll_set()` every `poll_every`; MANUAL waits for `request_update()` (HOME UPDATE button or `u`). `Live::round` counts the replies of the round in flight and `last_update` feeds the HOME strip. When `Sim::set_bool/set_num/run` see a live mode they call `live_action` instead of simulating. Fields the real source lacks hold `NaN` or `live::NA_U32` and render as `n/a`.

Adding a control that talks to the flight software: add the row in `controls.rs`, a builder in `proto.rs` (text and bytes together, with a unit test), a `plan()` arm and, if it returns data, a `decode()` arm. Check it with `tools/fsw_rig.py` and `daltui --compare`.

`src/main.rs` handles the CLI, terminal setup and teardown, the interactive loop, and `--dump`, which drives `State` with scripted keys and renders into a test buffer.

## Conventions specific to this repo

- Keep input handling in `app.rs`, simulation rules in `sim.rs`, and drawing in `ui/`.
- Frequency, voltage and speed values are *staged*: changing them only updates the pending value on screen, and the matching Set / Jog action sends it. This mirrors the firmware CLI, so preserve it when adding controls.
- Capture file paths and formats mimic the real tools (USB JPEG 1280x720, MIPI RAW 5120x3840), so keep them in sync with the firmware if those change.
- Commit subjects are short and imperative, prefixed with the area (`sim: enforce motor rail interlock`).
