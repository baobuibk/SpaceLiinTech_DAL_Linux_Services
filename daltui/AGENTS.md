# Repository Guidelines

## Project Structure & Module Organization

`daltui` is a Rust 2021 terminal UI for the DAL OBC, EXP, and PDU subsystems. It currently uses simulated data only. `src/main.rs` handles the CLI, terminal lifecycle, and headless `--dump` mode. `src/app.rs` owns UI state and keyboard input; `src/controls.rs` defines control tables; `src/sim.rs` holds subsystem state, behavior, and interlocks. Rendering lives in `src/ui/`, split into home, panels, controls, and log views. There are no separate test or asset directories yet. Keep generated files in `target/`, which is ignored by Git.

## Build, Test, and Development Commands

- `cargo build` builds a debug binary; `cargo build --release` builds the optimized binary described in `README.md`. Add `--offline` when using the local Cargo cache.
- `cargo run` starts the interactive simulation. Use a terminal at least 100x35 cells.
- `cargo run -- --dump exp 120x40 down,enter` renders a view as text without a TTY. Use this to inspect layout and keyboard behavior.
- `cargo fmt --check` checks formatting; `cargo clippy --all-targets` checks common Rust issues; `cargo test` runs Rust tests.

## Coding Style & Naming Conventions

Follow standard `rustfmt` formatting and four-space indentation. Use `snake_case` for modules, functions, and fields; `UpperCamelCase` for types and enum variants; and uppercase names for constants. Keep input handling in `app.rs`, simulation rules in `sim.rs`, and drawing in `ui/`. When adding a control, define its stable, subsystem-prefixed ID in `controls.rs` (for example, `exp.motor`) and handle that ID in `sim.rs`.

## Testing Guidelines

No tests or coverage target are currently defined. Add focused `#[cfg(test)]` unit tests near simulation or input logic when changing behavior, with descriptive `snake_case` test names. Run `cargo test` and check affected tabs with `--dump`; inspect the interactive view when layout or terminal handling changes.

## Commit & Pull Request Guidelines

The available Git history has one initial commit, so it does not establish a message convention. Use short, imperative subjects that name the changed area, such as `sim: enforce motor rail interlock`. In pull requests, explain the behavior changed, list validation commands, and include a terminal capture or `--dump` excerpt for visible UI changes. Link a related issue when one exists.
