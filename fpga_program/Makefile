DEPLOY_DIR ?= ../deploy/fpga_program
TARGET = build-target/fpga_program
SOURCE = files/fpga_program.sh

.PHONY: all check target deploy

all: target

check:
	sh -n $(SOURCE)
	python3 -m unittest discover -s tests -q

$(TARGET): $(SOURCE)
	install -d build-target
	install -m 0755 $(SOURCE) $(TARGET)

target: check $(TARGET)

deploy: target
	install -d "$(DEPLOY_DIR)"
	install -m 0755 $(TARGET) "$(DEPLOY_DIR)/fpga_program"
