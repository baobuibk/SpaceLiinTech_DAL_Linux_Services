HOSTCC ?= cc
DEPLOY_DIR ?= ../deploy/spi_flash_s25
SOURCE = files/spi_flash_s25.c
TARGET = build-target/spi_flash_s25
HOST_FLAGS = -std=c11 -O1 -g -Wall -Wextra -Werror

.PHONY: all check target deploy

all: target

build-host/spi_flash_s25: $(SOURCE)
	install -d build-host
	$(HOSTCC) $(HOST_FLAGS) -o $@ $<

build-host/test_spi_flash_s25: tests/test_spi_flash_s25.c $(SOURCE)
	install -d build-host
	$(HOSTCC) $(HOST_FLAGS) -o $@ $<

build-host/test_board_sequence: tests/test_board_sequence.c $(SOURCE)
	install -d build-host
	$(HOSTCC) $(HOST_FLAGS) -o $@ $<

check: build-host/spi_flash_s25 build-host/test_spi_flash_s25 build-host/test_board_sequence
	python3 tests/test_cli.py build-host/spi_flash_s25
	build-host/test_spi_flash_s25
	build-host/test_board_sequence

$(TARGET): $(SOURCE)
	install -d build-target
	$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -o $@ $< $(LDFLAGS)

target: check
	./build-sdk.sh $(TARGET)

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 $(TARGET) "$(DEPLOY_DIR)/spi_flash_s25"
