HOSTCC ?= cc
SDK_ROOT ?= /home/buildmachine/oecore-obcv110
DEPLOY_DIR ?= ../deploy/rs422_service
SOURCE = ../../sources/meta-src-dalobcv110/recipes-app/rs422-service/files/rs422_service.c
UNIT = ../../sources/meta-src-dalobcv110/recipes-app/rs422-service/files/rs422.service
FLAGS = -std=c11 -D_GNU_SOURCE -Wall -Wextra -O2

.PHONY: all check target sdk-build deploy clean
all: build-host/rs422-service

build-host/rs422-service: $(SOURCE)
	install -d build-host
	$(HOSTCC) $(FLAGS) -o $@ $<

check: build-host/rs422-service
	@test -s $(UNIT)

build-target/rs422-service: $(SOURCE) build-sdk.sh
	SDK_ROOT="$(SDK_ROOT)" ./build-sdk.sh

sdk-build:
	install -d build-target
	$(CC) $(CFLAGS) $(FLAGS) -o build-target/rs422-service $(SOURCE) $(LDFLAGS)

target: check build-target/rs422-service

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 build-target/rs422-service "$(DEPLOY_DIR)/rs422-service"
	install -m 0644 $(UNIT) "$(DEPLOY_DIR)/rs422.service"

clean:
	rm -rf build-host build-target
