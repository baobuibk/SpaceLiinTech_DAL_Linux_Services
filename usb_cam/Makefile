DEPLOY_DIR ?= ../deploy/usb_cam
SOURCE = files/usb_cam.py
CONFIG = files/usb-cam.json
TARGET = build-target/usb_cam

.PHONY: all check target deploy

all: target

check:
	python3 -m unittest discover -s tests -q

$(TARGET): $(SOURCE) $(CONFIG) build_bundle.py
	install -d build-target
	python3 build_bundle.py $(SOURCE) $(CONFIG) $(TARGET)
	chmod 0755 $(TARGET)

target: check $(TARGET)

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 $(TARGET) "$(DEPLOY_DIR)/usb_cam"
