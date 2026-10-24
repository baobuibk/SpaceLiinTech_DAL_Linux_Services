.PHONY: all host target deploy

all: host

host:
	./build.sh host

# Cross-compile with /home/buildmachine/oecore-obcv110 (or PDU_SDK_ROOT).
target:
	./build.sh target

# Stage the ARM executable for transfer to the board.
deploy: target
	install -d ../deploy
	install -m 0755 target/armv7-unknown-linux-gnueabihf/release/dal-pdu-gui ../deploy/dal-pdu-gui
