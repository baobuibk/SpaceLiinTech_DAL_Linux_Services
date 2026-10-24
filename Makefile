# Stage OBC bootloader clients under service/deploy/.

.PHONY: deploy deploy-samv71 deploy-avr

deploy: deploy-samv71 deploy-avr

deploy-samv71:
	$(MAKE) -C bootloader_ota/sam_xbld deploy

deploy-avr:
	$(MAKE) -C bootloader_ota/avr_xbld deploy
