HOSTCC ?= cc
SDK_ROOT ?= /home/buildmachine/oecore-obcv110
DEPLOY_DIR ?= ../deploy/dal_obc_fsw
COMMON_FLAGS = -std=c11 -D_GNU_SOURCE -Wall -Wextra -O2 -Iinclude \
               -Iinclude/services/obc_hk \
               -Ilib/bee_dal_proto/include -Ilib/bee_dal_files/include \
               -Ilib/libutils/include -pthread
SOURCES = src/main.c src/services/hk_service.c src/services/bee_dal_tmtc_service.c \
          src/services/exp_csp_can.c src/services/exp_csp_uart.c src/services/exp_csp_service.c \
          src/services/exp_data_service.c \
          src/services/obc_hk_collector.c \
          src/services/obc_hk/hk_temp_src.c src/services/obc_hk/hk_bme_src.c \
          src/services/obc_hk/hk_csv.c src/services/obc_hk/hk_fs.c \
          src/services/obc_hk/pac1934.c \
          src/services/flight_console_service.c \
          src/services/port_config.c \
          src/services/gui_state.c \
          src/services/sensor_stat.c \
          src/services/uvc_capture.c \
          src/services/power_en.c \
          src/services/power_stat.c \
          src/services/gui_hw.c \
          src/services/exp_uart_service.c \
          src/services/obc_hk_wire.c src/services/obc_snapshot.c \
          lib/bee_dal_proto/src/bee_dal_proto.c \
          lib/bee_dal_proto/src/bee_dal_crc16.c \
          lib/bee_dal_proto/src/bee_dal_crc32.c \
          lib/bee_dal_files/src/bee_dal_files.c \
          lib/libutils/src/error.c
HEADERS = $(wildcard include/*.h include/services/*.h include/services/obc_hk/*.h \
          lib/bee_dal_proto/include/*.h lib/bee_dal_files/include/*.h \
          lib/libutils/include/utils/*.h)

.PHONY: fsw-sim-test all check target deploy clean sdk-build
all: build-host/dal_obc_fsw build-host/dal_flight_console

build-host/dal_flight_console: src/flight_console_client.c $(HEADERS) Makefile
	install -d build-host
	$(HOSTCC) $(COMMON_FLAGS) -o $@ src/flight_console_client.c

build-host/dal_obc_fsw: $(SOURCES) $(HEADERS) Makefile
	install -d build-host
	$(HOSTCC) $(COMMON_FLAGS) -o $@ $(SOURCES) -lm

build-host/test_obc_hk_wire: tests/test_obc_hk_wire.c src/services/obc_hk_wire.c \
                             lib/bee_dal_proto/src/bee_dal_proto.c \
                             lib/bee_dal_proto/src/bee_dal_crc16.c $(HEADERS)
	install -d build-host
	$(HOSTCC) $(COMMON_FLAGS) -o $@ tests/test_obc_hk_wire.c \
	    src/services/obc_hk_wire.c lib/bee_dal_proto/src/bee_dal_proto.c \
	    lib/bee_dal_proto/src/bee_dal_crc16.c -lm

build-host/test_obc_snapshot: tests/test_obc_snapshot.c src/services/obc_snapshot.c $(HEADERS)
	install -d build-host
	$(HOSTCC) $(COMMON_FLAGS) -o $@ tests/test_obc_snapshot.c \
	    src/services/obc_snapshot.c -lm

build-host/test_hk_csv_files: tests/test_hk_csv_files.c \
                             lib/bee_dal_files/src/bee_dal_files.c \
                             lib/bee_dal_proto/src/bee_dal_crc32.c $(HEADERS)
	install -d build-host
	$(HOSTCC) $(COMMON_FLAGS) -o $@ tests/test_hk_csv_files.c \
	    lib/bee_dal_files/src/bee_dal_files.c \
	    lib/bee_dal_proto/src/bee_dal_crc32.c -lm

build-host/test_exp_uart_probe: tests/exp_uart_probe.c src/services/exp_uart_service.c $(HEADERS)
	install -d build-host
	$(HOSTCC) $(COMMON_FLAGS) -o $@ tests/exp_uart_probe.c src/services/exp_uart_service.c

build-host/test_port_config: tests/test_port_config.c src/services/port_config.c $(HEADERS)
	install -d build-host
	$(HOSTCC) $(COMMON_FLAGS) -o $@ tests/test_port_config.c src/services/port_config.c

check: build-host/dal_obc_fsw build-host/dal_flight_console build-host/test_obc_hk_wire \
       build-host/test_obc_snapshot build-host/test_hk_csv_files build-host/test_exp_uart_probe build-host/test_port_config
	./build-host/dal_obc_fsw --help >/dev/null
	./build-host/dal_flight_console --help >/dev/null
	./build-host/test_obc_hk_wire
	./build-host/test_obc_snapshot
	./build-host/test_hk_csv_files
	./build-host/test_port_config
	python3 tests/test_exp_uart.py
	python3 tests/test_flight_console.py
	python3 tests/test_exp_csp.py
	python3 tests/test_exp_csp_failover.py
	python3 tests/test_exp_uart_fallback.py
	python3 tests/test_exp_data.py
	python3 tests/test_obc_collector.py
	python3 tests/test_rs422_socket.py
	python3 tests/test_quiet_housekeeping.py

# Automated RS-422/console framework (tests/fsw_auto); ~1 min, not part of `check`.
fsw-sim-test: build-host/dal_obc_fsw build-host/dal_flight_console
	python3 -W ignore tests/fsw_auto/run_all.py $(FSW_TEST_ARGS)

sdk-build:
	install -d build-target
	$(CC) $(CFLAGS) $(COMMON_FLAGS) -o build-target/dal_obc_fsw $(SOURCES) $(LDFLAGS) -lm
	$(CC) $(CFLAGS) $(COMMON_FLAGS) -o build-target/dal_flight_console src/flight_console_client.c $(LDFLAGS)

target:
	SDK_ROOT="$(SDK_ROOT)" ./build-sdk.sh

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 build-target/dal_obc_fsw "$(DEPLOY_DIR)/dal_obc_fsw"
	install -m 0755 build-target/dal_flight_console "$(DEPLOY_DIR)/dal_flight_console"
	install -m 0644 files/dal_obc_fsw.service "$(DEPLOY_DIR)/dal_obc_fsw.service"
	install -m 0644 files/dal-obc-fsw.env "$(DEPLOY_DIR)/dal-obc-fsw.env"

clean:
	rm -rf build-host build-target
