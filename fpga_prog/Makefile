CC ?= cc
HOSTCC ?= cc
CFLAGS ?= -O2 -g
WARN = -std=c11 -Wall -Wextra -Werror -Wpedantic
BUILD ?= build
PREFIX ?= /usr/local

.PHONY: all test install
all: $(BUILD)/fpga_prog
$(BUILD)/fpga_prog: src/main.c src/programmer.c src/programmer.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) src/main.c src/programmer.c $(LDFLAGS) -o $@
build-host/test_programmer: tests/test_programmer.c src/programmer.c src/programmer.h
	mkdir -p build-host
	$(HOSTCC) -O1 -g $(WARN) -Isrc tests/test_programmer.c src/programmer.c -o $@
test: build-host/test_programmer
	./build-host/test_programmer
	$(MAKE) CC=$(HOSTCC) CFLAGS='-O1 -g' CPPFLAGS= LDFLAGS= BUILD=build-host all
	python3 tests/test_cli.py
install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(BUILD)/fpga_prog $(DESTDIR)$(PREFIX)/bin/fpga_prog
