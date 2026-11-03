HOSTCC ?= cc
DEPLOY_DIR ?= ../deploy/bme_service
SOURCES = src/main.c src/app_args.c src/bme688/bme688.c src/bosch/bme68x.c \
          src/bme_i2c/bme_i2c.c src/bme_fs/bme_fs.c
HEADERS = $(wildcard include/*.h include/bosch/*.h)
FLAGS = -std=c11 -D_GNU_SOURCE -Iinclude -Iinclude/bosch -Wall -Wextra -Wno-unused-parameter

.PHONY: all check target deploy

all: target

build-host/bme_service: $(SOURCES) $(HEADERS)
	install -d build-host
	$(HOSTCC) $(FLAGS) -O2 -o $@ $(SOURCES) -lm

build-host/test_bme688: tests/test_bme688.c src/bme688/bme688.c src/bosch/bme68x.c src/bme_i2c/bme_i2c.c $(HEADERS)
	install -d build-host
	$(HOSTCC) $(FLAGS) -O1 -g -o $@ tests/test_bme688.c \
		src/bme688/bme688.c src/bosch/bme68x.c src/bme_i2c/bme_i2c.c -lm

check: build-host/bme_service build-host/test_bme688
	build-host/bme_service --help >/dev/null 2>&1
	build-host/test_bme688

build-target/bme_service: $(SOURCES) $(HEADERS)
	install -d build-target
	$(CC) $(CFLAGS) $(FLAGS) -O2 -o $@ $(SOURCES) $(LDFLAGS) -lm

target: check
	./build-sdk.sh build-target/bme_service

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 build-target/bme_service "$(DEPLOY_DIR)/bme_service"
	install -m 0644 files/bme.service "$(DEPLOY_DIR)/bme.service"
	install -m 0644 LICENSE-BOSCH "$(DEPLOY_DIR)/LICENSE-BOSCH"
