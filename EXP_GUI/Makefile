CARGO ?= cargo
PREFIX ?= $(HOME)/.local
DESTDIR ?=
BIN := target/release/exp_tui
RUST_TARGET ?= armv7-unknown-linux-musleabihf
DEPLOY_DIR ?= ../deploy/EXP_GUI

.PHONY: all build target deploy run sim run-desktop check check-desktop preview install clean

all: build

build:
	$(CARGO) build --locked --release --bin exp_tui

target:
	$(CARGO) build --locked --release --target $(RUST_TARGET) --bin exp_tui

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 "target/$(RUST_TARGET)/release/exp_tui" "$(DEPLOY_DIR)/exp_tui"

run:
	$(CARGO) run --locked --release --bin exp_tui -- $(ARGS)

sim:
	$(CARGO) run --locked --release --bin exp_tui -- --demo

run-desktop:
	$(CARGO) run --locked --release --features desktop --bin exp_test_gui

check:
	$(CARGO) test --locked --bin exp_tui

check-desktop:
	$(CARGO) test --locked --features desktop --bin exp_test_gui

# Opens the built-in motor preview without connecting to hardware.
preview:
	$(CARGO) run --locked --release --features desktop --bin exp_test_gui -- --motor-preview

install: build
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 0755 "$(BIN)" "$(DESTDIR)$(PREFIX)/bin/exp_tui"

clean:
	$(CARGO) clean
