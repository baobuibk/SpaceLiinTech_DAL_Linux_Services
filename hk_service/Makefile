HOSTCC ?= cc
DEPLOY_DIR ?= ../deploy/hk_service
TARGET = build-target/hk_service
HOST_TARGET = build-host/hk_service
SOURCES = src/main.c src/app_args.c src/hk_can/hk_can.c src/hk_xio/hk_xio.c \
          src/hk_temp_src/hk_temp_src.c src/hk_csv/hk_csv.c src/hk_fs/hk_fs.c \
          src/pac1934/pac1934.c src/hk_bme_src/hk_bme_src.c \
          src/hk_interval/hk_interval.c \
          src/hk_data_ready/hk_data_ready.c
HEADERS = $(wildcard include/*.h)
COMMON_FLAGS = -std=c11 -D_GNU_SOURCE -Iinclude -Wall -Wextra -Wno-unused-parameter

.PHONY: all check target deploy

all: target

$(HOST_TARGET): $(SOURCES) $(HEADERS)
	install -d build-host
	$(HOSTCC) $(COMMON_FLAGS) -O2 -o $@ $(SOURCES)

build-host/test_pac1934: tests/test_pac1934.c src/pac1934/pac1934.c include/pac1934.h
	install -d build-host
	$(HOSTCC) $(COMMON_FLAGS) -O1 -g -o $@ tests/test_pac1934.c src/pac1934/pac1934.c

build-host/test_sources: tests/test_sources.c src/hk_bme_src/hk_bme_src.c src/hk_temp_src/hk_temp_src.c include/hk_bme_src.h include/hk_temp_src.h
	install -d build-host
	$(HOSTCC) $(COMMON_FLAGS) -O1 -g -o $@ tests/test_sources.c src/hk_bme_src/hk_bme_src.c src/hk_temp_src/hk_temp_src.c -lm

build-host/test_csv: tests/test_csv.c src/hk_csv/hk_csv.c include/hk_csv.h
	install -d build-host
	$(HOSTCC) $(COMMON_FLAGS) -O1 -g -o $@ tests/test_csv.c src/hk_csv/hk_csv.c

build-host/test_exp_sensors: tests/test_exp_sensors.c src/hk_can/hk_can.c include/hk_can.h
	install -d build-host
	$(HOSTCC) $(COMMON_FLAGS) -O1 -g -o $@ tests/test_exp_sensors.c src/hk_can/hk_can.c

check: $(HOST_TARGET) build-host/test_pac1934 build-host/test_sources build-host/test_csv build-host/test_exp_sensors
	$(HOST_TARGET) --help >/dev/null 2>&1
	build-host/test_pac1934
	build-host/test_sources
	build-host/test_csv
	build-host/test_exp_sensors

$(TARGET): $(SOURCES) $(HEADERS)
	install -d build-target
	$(CC) $(CFLAGS) $(COMMON_FLAGS) -O2 -o $@ $(SOURCES) $(LDFLAGS)

target: check
	./build-sdk.sh $(TARGET)

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 $(TARGET) "$(DEPLOY_DIR)/hk_service"
	install -m 0644 files/hk_service.service "$(DEPLOY_DIR)/hk_service.service"
