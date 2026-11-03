HOSTCC ?= cc
SDK_ROOT ?= /home/buildmachine/oecore-obcv110
DEPLOY_DIR ?= ../deploy/dal
CFLAGS_COMMON = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -O2

.PHONY: all check target deploy clean sdk-build
all: build-host/dal

build-host/dal: src/dal.c
	install -d build-host
	$(HOSTCC) $(CFLAGS_COMMON) -o $@ $<

check: build-host/dal
	python3 tests/test_dal.py build-host/dal

build-target/dal: src/dal.c build-sdk.sh
	SDK_ROOT="$(SDK_ROOT)" ./build-sdk.sh

sdk-build:
	install -d build-target
	$(CC) $(CFLAGS) $(CFLAGS_COMMON) -o build-target/dal src/dal.c $(LDFLAGS)

target: check build-target/dal

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 build-target/dal "$(DEPLOY_DIR)/dal"

clean:
	rm -rf build-host build-target
