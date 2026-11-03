# Build and stage OBC service tools under service/deploy/.

DEPLOY_DIRS := samv71_xbld avr_xbld spi_flash_s25 spi_eeprom_cat93 rs422_service dal_pdu \
               fpga_program usb_cam hk_service temp_service bme_service \
               dal dal_obc_fsw daltui OBC_GUI EXP_GUI exp_test_gui lite-client gs-client

DALTUI_SDK_DIR ?= /home/buildmachine/oecore-obcv110

BUILD_DIRS := fpga_program/build-target \
              spi_flash_s25/build-host spi_flash_s25/build-target \
              spi_eeprom_cat93/build-host spi_eeprom_cat93/build-target \
              usb_cam/build-target \
              hk_service/build-host hk_service/build-target \
              temp_service/build-host temp_service/build-target \
              bme_service/build-host bme_service/build-target \
              daltui/target

.PHONY: deploy clean_deploy clean deploy-samv71 deploy-avr deploy-fpga-program deploy-spi-flash-s25 deploy-spi-eeprom-cat93 deploy-pdu-gui deploy-usb-cam deploy-hk-service deploy-temp-service deploy-bme-service deploy-dal deploy-dal-obc-fsw deploy-daltui deploy-obc-gui deploy-exp-gui deploy-exp-test-gui deploy-lite-client deploy-gs-client package-sensor-sources package-dal-obc-fsw

deploy: deploy-samv71 deploy-avr deploy-spi-flash-s25 deploy-spi-eeprom-cat93 deploy-fpga-program deploy-pdu-gui deploy-usb-cam deploy-hk-service deploy-temp-service deploy-bme-service deploy-dal deploy-dal-obc-fsw deploy-daltui deploy-obc-gui deploy-exp-gui deploy-exp-test-gui deploy-lite-client deploy-gs-client

# Remove only artifacts staged by the deploy target. Keep deploy/Makefile,
# deploy/README.md, and separately staged tools such as dal_pdu.
clean_deploy:
	rm -rf -- $(addprefix deploy/,$(DEPLOY_DIRS))

# Remove host and target build output for the projects in deploy.
clean:
	$(MAKE) -C bootloader_ota/sam_xbld clean
	$(MAKE) -C bootloader_ota/avr_xbld clean
	$(MAKE) -C dal clean
	$(MAKE) -C dal_obc_fsw clean
	$(MAKE) -C OBC_GUI clean
	$(MAKE) -C EXP_GUI clean
	$(MAKE) -C exp_test_gui clean
	$(MAKE) -C lite-client clean
	$(MAKE) -C gs-client clean
	rm -rf -- $(BUILD_DIRS)

deploy-dal:
	$(MAKE) -C dal deploy

deploy-dal-obc-fsw:
	$(MAKE) -C dal_obc_fsw deploy

deploy-daltui:
	daltui/tools/build-board.sh "$(DALTUI_SDK_DIR)"
	install -d deploy/daltui
	install -m 0755 daltui/target/armv7-unknown-linux-gnueabihf/release/daltui deploy/daltui/daltui

# The full deploy package includes the GUI and its camera tool.
deploy-obc-gui:
	$(MAKE) -C OBC_GUI deploy

deploy-exp-gui:
	$(MAKE) -C EXP_GUI deploy

deploy-exp-test-gui:
	$(MAKE) -C exp_test_gui deploy

deploy-lite-client:
	$(MAKE) -C lite-client deploy

deploy-gs-client:
	$(MAKE) -C gs-client deploy

deploy-samv71:
	$(MAKE) -C bootloader_ota/sam_xbld deploy

deploy-avr:
	$(MAKE) -C bootloader_ota/avr_xbld deploy

deploy-fpga-program:
	$(MAKE) -C fpga_program deploy

deploy-pdu-gui:
	$(MAKE) -C pdu-gui deploy

deploy-spi-flash-s25:
	$(MAKE) -C spi_flash_s25 deploy

deploy-spi-eeprom-cat93:
	$(MAKE) -C spi_eeprom_cat93 deploy

deploy-usb-cam:
	$(MAKE) -C usb_cam deploy

deploy-hk-service:
	$(MAKE) -C hk_service deploy

deploy-temp-service:
	$(MAKE) -C temp_service deploy

deploy-bme-service:
	$(MAKE) -C bme_service deploy

package-sensor-sources:
	python3 package_sensor_sources.py

package-dal-obc-fsw:
	python3 package_dal_obc_fsw.py
