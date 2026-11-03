DEPLOY_DIR ?= ../deploy/gs-client
VENDOR_BIN := vendor/csp-client
TARGET_BIN := build-target/csp-client

.PHONY: all target deploy check clean

all: target

target: $(TARGET_BIN)

$(TARGET_BIN): $(VENDOR_BIN) Makefile
	install -d build-target
	install -m 0755 "$(VENDOR_BIN)" "$@"

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 "$(TARGET_BIN)" "$(DEPLOY_DIR)/csp-client"

check: target
	@file "$(TARGET_BIN)" | grep -q 'ELF 32-bit.*ARM'
	@readelf -l "$(TARGET_BIN)" | grep -q '/usr/lib/ld-linux-armhf.so.3'

clean:
	rm -rf build-target
