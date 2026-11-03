HOSTCC ?= cc
DEPLOY_DIR ?= ../deploy/spi_eeprom_cat93
SOURCE = files/spi_eeprom_cat93.c
TARGET = build-target/spi_eeprom_cat93
HOST_FLAGS = -std=c11 -O2 -Wall -Wextra -Werror

.PHONY: all check target deploy

all: target

build-host/spi_eeprom_cat93: $(SOURCE)
	install -d build-host
	$(HOSTCC) $(HOST_FLAGS) -o $@ $<

build-host/test_protocol: tests/test_protocol.c $(SOURCE)
	install -d build-host
	$(HOSTCC) $(HOST_FLAGS) -o $@ $<

check: build-host/spi_eeprom_cat93 build-host/test_protocol
	build-host/spi_eeprom_cat93 --help >/dev/null
	build-host/test_protocol

$(TARGET): $(SOURCE)
	install -d build-target
	$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -o $@ $< $(LDFLAGS)

target: check
	./build-sdk.sh $(TARGET)

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 $(TARGET) "$(DEPLOY_DIR)/spi_eeprom_cat93"
