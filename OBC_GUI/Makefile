CARGO ?= cargo
RUST_TARGET ?= armv7-unknown-linux-musleabihf
DEPLOY_DIR ?= ../deploy/OBC_GUI

.PHONY: all check host_sim target deploy clean

all:
	$(CARGO) build --locked --release

check:
	$(CARGO) test --locked

host_sim: all
	target/release/obc_gui host-sim

target:
	$(CARGO) build --locked --release --target $(RUST_TARGET)

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 "target/$(RUST_TARGET)/release/obc_gui" "$(DEPLOY_DIR)/obc_gui"

clean:
	$(CARGO) clean
