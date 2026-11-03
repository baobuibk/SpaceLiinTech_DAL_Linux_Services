.PHONY: all host target check desktop check-desktop run demo deploy clean

all: host

host:
	./build.sh host

# Build the ARMv7 GUI against the obcv110 Yocto SDK.
target:
	./build.sh target

check:
	cargo test --locked --offline --bin exp_test_gui

desktop:
	cargo build --locked --release --features desktop --bin exp_test_gui_desktop

check-desktop:
	cargo test --locked --offline --features desktop --bin exp_test_gui_desktop

run: host
	target/release/exp_test_gui $(ARGS)

demo: host
	target/release/exp_test_gui --demo

deploy: target
	install -d ../deploy/exp_test_gui
	install -m 0755 target/armv7-unknown-linux-gnueabihf/release/exp_test_gui ../deploy/exp_test_gui/exp_test_gui

clean:
	cargo clean
