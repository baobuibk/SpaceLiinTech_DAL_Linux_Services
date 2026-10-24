DEPLOY_DIR ?= ../../deploy/avr_xbld

.PHONY: all check host target deploy clean

all: host

check:
	python3 -m py_compile avr_xbld boot_host.py
	python3 -m unittest discover -s tests -v

host: check build-host/avr_xbld
	build-host/avr_xbld --help >/dev/null

target: check build-target/avr_xbld
	build-target/avr_xbld --help >/dev/null

# A Python zipapp carries both source files in one executable. Python source
# is architecture-independent; the OBC supplies Python 3 and pyserial.
build-%/avr_xbld: avr_xbld boot_host.py
	install -d "build-$*/pkg"
	install -m 0644 avr_xbld "build-$*/pkg/__main__.py"
	install -m 0644 boot_host.py "build-$*/pkg/boot_host.py"
	python3 -m zipapp "build-$*/pkg" -o "$@" -p '/usr/bin/env python3'
	chmod 0755 "$@"

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 build-target/avr_xbld "$(DEPLOY_DIR)/avr_xbld"

clean:
	rm -rf build-host build-target
