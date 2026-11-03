# exp_tui -- terminal UI for the DAL EXP board (host simulation + board build)
CARGO       ?= cargo
BIN         := exp_tui
RUST_TARGET ?= armv7-unknown-linux-musleabihf
DEPLOY_DIR  ?= ../deploy/exp_tui
OFFLINE     ?=
CARGOFLAGS  := --release $(if $(OFFLINE),--offline,)

.PHONY: all host sim run target deploy check dump clean help

all: host

help:
	@echo "make host      native release build  -> target/release/$(BIN)"
	@echo "make sim       run on host with the built-in simulator (no hardware)"
	@echo "make run ARGS='--port /dev/ttyUSB0'   run on host against a real UART2"
	@echo "make target    cross-compile          -> target/$(RUST_TARGET)/release/$(BIN)"
	@echo "make deploy    target + copy to $(DEPLOY_DIR)"
	@echo "make check     unit tests"
	@echo "make dump PAGE=heaters   print one rendered page as text (no tty needed)"

host:
	$(CARGO) build $(CARGOFLAGS) --bin $(BIN)

sim:
	$(CARGO) run $(CARGOFLAGS) --bin $(BIN) -- --sim

run:
	$(CARGO) run $(CARGOFLAGS) --bin $(BIN) -- $(ARGS)

target:
	$(CARGO) build $(CARGOFLAGS) --target $(RUST_TARGET) --bin $(BIN)

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 "target/$(RUST_TARGET)/release/$(BIN)" "$(DEPLOY_DIR)/$(BIN)"

check:
	$(CARGO) test $(if $(OFFLINE),--offline,)

dump:
	$(CARGO) run $(CARGOFLAGS) --bin $(BIN) -- --dump "$(PAGE)" $(SIZE)

clean:
	$(CARGO) clean
