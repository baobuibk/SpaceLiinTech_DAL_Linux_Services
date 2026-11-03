HOSTCC ?= cc
DEPLOY_DIR ?= ../deploy/temp_service
SOURCES = src/main.c src/app_args.c src/temp_adc/temp_adc.c src/temp_fs/temp_fs.c
HEADERS = $(wildcard include/*.h)
FLAGS = -std=c11 -D_GNU_SOURCE -Iinclude -Wall -Wextra -Wno-unused-parameter

.PHONY: all check target deploy

all: target

build-host/temp_service: $(SOURCES) $(HEADERS)
	install -d build-host
	$(HOSTCC) $(FLAGS) -O2 -o $@ $(SOURCES) -lm

build-host/test_adc: tests/test_adc.c src/temp_adc/temp_adc.c include/temp_adc.h
	install -d build-host
	$(HOSTCC) $(FLAGS) -O1 -g -o $@ tests/test_adc.c src/temp_adc/temp_adc.c -lm

check: build-host/temp_service build-host/test_adc
	build-host/temp_service --help >/dev/null 2>&1
	build-host/test_adc

build-target/temp_service: $(SOURCES) $(HEADERS)
	install -d build-target
	$(CC) $(CFLAGS) $(FLAGS) -O2 -o $@ $(SOURCES) $(LDFLAGS) -lm

target: check
	./build-sdk.sh build-target/temp_service

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 build-target/temp_service "$(DEPLOY_DIR)/temp_service"
	install -m 0644 files/temp.service "$(DEPLOY_DIR)/temp.service"
