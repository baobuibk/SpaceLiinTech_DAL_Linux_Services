SDK_ROOT ?= /home/buildmachine/oecore-obcv110
PYTHON ?= python3
DEPLOY_DIR ?= ../deploy/lite-client

.PHONY: all target deploy check clean

all:
	$(PYTHON) ./waf configure --out=build-host
	$(PYTHON) ./waf build_lite --out=build-host

target:
	SDK_ROOT="$(SDK_ROOT)" PYTHON="$(PYTHON)" ./build-target.sh

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 build-target/csp-client-lite "$(DEPLOY_DIR)/csp-client-lite"
	install -m 0755 lite-client "$(DEPLOY_DIR)/lite-client"

check: all
	./build-host/csp-client-lite --help >/dev/null
	bash -n lite-client

clean:
	rm -rf build-host build-target
