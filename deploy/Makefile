# Run this directory on the DAL OBC as root. DESTDIR supports offline staging.
DESTDIR ?=
BIN_DIR ?= /home/root/tools/bin
DAL_LINK_DIR ?= /usr/bin
UNIT_DIR ?= /usr/lib/systemd/system
SYSTEMCTL ?= systemctl
ACTIVE_UNITS := bme.service temp.service dal_obc_fsw.service
STOP_UNITS := dal_obc_fsw.service hk_service.service temp.service bme.service rs422.service
CONSOLE_SOCKET ?= /run/dal/dal_obc_fsw/console.sock

STAGED_FILES := bme_service/bme_service bme_service/bme.service bme_service/LICENSE-BOSCH \
                temp_service/temp_service temp_service/temp.service \
                hk_service/hk_service hk_service/hk_service.service \
                dal/dal dal_obc_fsw/dal_obc_fsw dal_obc_fsw/dal_flight_console dal_obc_fsw/dal_obc_fsw.service dal_obc_fsw/dal-obc-fsw.env daltui/daltui \
                OBC_GUI/obc_gui EXP_GUI/exp_tui exp_test_gui/exp_test_gui usb_cam/usb_cam dal_pdu/dal-pdu-gui \
                fpga_program/fpga_program spi_flash_s25/spi_flash_s25 \
                spi_eeprom_cat93/spi_eeprom_cat93 samv71_xbld/samv71_xbld avr_xbld/avr_xbld \
                lite-client/csp-client-lite lite-client/lite-client gs-client/csp-client

.PHONY: install install-clients preflight board_preflight stop start status

board_preflight:
	@set -eu; \
	if [ -n "$(DESTDIR)" ]; then \
	    echo "This target runs on the board; omit DESTDIR." >&2; exit 1; \
	fi; \
	if [ "$$(id -u)" -ne 0 ]; then \
	    echo "Run make as root on the DAL OBC." >&2; exit 1; \
	fi; \
	case "$$(uname -m)" in arm*|aarch64) ;; \
	    *) echo "Refusing to manage ARM services on this host." >&2; exit 1 ;; \
	esac

preflight:
	@set -eu; \
	for path in $(STAGED_FILES); do \
	    if [ ! -f "$(CURDIR)/$$path" ]; then \
	        echo "Missing staged file: $$path. Run make -C service deploy on the build host." >&2; \
	        exit 1; \
	    fi; \
	done; \
	for name in dal dal_flight_console; do \
	    if [ -e "$(DESTDIR)$(DAL_LINK_DIR)/$$name" ] && \
	       [ ! -L "$(DESTDIR)$(DAL_LINK_DIR)/$$name" ]; then \
	        echo "Refusing to replace existing $(DESTDIR)$(DAL_LINK_DIR)/$$name" >&2; exit 1; \
	    fi; \
	done; \
	if [ -z "$(DESTDIR)" ]; then \
	    if [ "$$(id -u)" -ne 0 ]; then \
	        echo "Run make install as root on the DAL OBC." >&2; exit 1; \
	    fi; \
	    case "$$(uname -m)" in arm*|aarch64) ;; \
	        *) echo "Refusing to install ARM binaries on this host; use DESTDIR for staging." >&2; exit 1 ;; \
	    esac; \
	fi

install-clients:
	@set -eu; \
	for path in lite-client/csp-client-lite lite-client/lite-client gs-client/csp-client; do \
	    test -f "$(CURDIR)/$$path" || { echo "Missing staged file: $$path" >&2; exit 1; }; \
	done; \
	if [ -z "$(DESTDIR)" ]; then \
	    test "$$(id -u)" -eq 0 || { echo "Run as root on the DAL OBC." >&2; exit 1; }; \
	    case "$$(uname -m)" in arm*|aarch64) ;; \
	        *) echo "Refusing to install ARM clients on this host." >&2; exit 1 ;; \
	    esac; \
	fi
	install -d "$(DESTDIR)$(BIN_DIR)"
	install -m 0755 lite-client/csp-client-lite "$(DESTDIR)$(BIN_DIR)/csp-client-lite.new"
	mv -f "$(DESTDIR)$(BIN_DIR)/csp-client-lite.new" "$(DESTDIR)$(BIN_DIR)/csp-client-lite"
	install -m 0755 lite-client/lite-client "$(DESTDIR)$(BIN_DIR)/lite-client.new"
	mv -f "$(DESTDIR)$(BIN_DIR)/lite-client.new" "$(DESTDIR)$(BIN_DIR)/lite-client"
	install -m 0755 gs-client/csp-client "$(DESTDIR)$(BIN_DIR)/csp-client.new"
	mv -f "$(DESTDIR)$(BIN_DIR)/csp-client.new" "$(DESTDIR)$(BIN_DIR)/csp-client"

stop: board_preflight
	@set -eu; \
	for unit in $(STOP_UNITS); do \
	    if $(SYSTEMCTL) cat "$$unit" >/dev/null 2>&1; then \
	        $(SYSTEMCTL) stop "$$unit"; \
	    fi; \
	done; \
	if $(SYSTEMCTL) cat rs422.service >/dev/null 2>&1; then \
	    $(SYSTEMCTL) disable rs422.service; \
	fi
	@if $(SYSTEMCTL) cat hk_service.service >/dev/null 2>&1; then \
	    $(SYSTEMCTL) disable hk_service.service; \
	fi

start: board_preflight
	$(SYSTEMCTL) daemon-reload
	@if $(SYSTEMCTL) cat hk_service.service >/dev/null 2>&1; then \
	    $(SYSTEMCTL) disable --now hk_service.service; \
	fi
	$(SYSTEMCTL) enable $(ACTIVE_UNITS)
	$(SYSTEMCTL) restart $(ACTIVE_UNITS)
	@set -eu; \
	for unit in $(ACTIVE_UNITS); do \
	    $(SYSTEMCTL) is-active --quiet "$$unit" || { \
	        $(SYSTEMCTL) status --no-pager "$$unit" || true; \
	        echo "Service failed to start: $$unit" >&2; exit 1; \
	    }; \
	done; \
	ready=0; \
	for attempt in 1 2 3 4 5; do \
	    if [ -S "$(CONSOLE_SOCKET)" ]; then ready=1; break; fi; \
	    sleep 1; \
	done; \
	if [ "$$ready" -ne 1 ]; then \
	    echo "Flight console socket did not appear: $(CONSOLE_SOCKET)" >&2; exit 1; \
	fi

status: board_preflight
	$(SYSTEMCTL) --no-pager status $(ACTIVE_UNITS)
	@test -S "$(CONSOLE_SOCKET)" && echo "Flight console ready: $(CONSOLE_SOCKET)"

install: preflight
	@if [ -z "$(DESTDIR)" ]; then $(MAKE) stop; fi
	install -d "$(DESTDIR)$(BIN_DIR)" "$(DESTDIR)$(DAL_LINK_DIR)" \
	           "$(DESTDIR)$(UNIT_DIR)" "$(DESTDIR)/usr/share/licenses/bme-service"
	install -m 0755 bme_service/bme_service "$(DESTDIR)$(BIN_DIR)/bme_service"
	install -m 0755 temp_service/temp_service "$(DESTDIR)$(BIN_DIR)/temp_service"
	install -m 0755 hk_service/hk_service "$(DESTDIR)$(BIN_DIR)/hk_service"
	install -m 0755 dal/dal "$(DESTDIR)$(BIN_DIR)/dal"
	install -m 0755 dal_obc_fsw/dal_obc_fsw "$(DESTDIR)$(BIN_DIR)/dal_obc_fsw"
	install -m 0755 dal_obc_fsw/dal_flight_console "$(DESTDIR)$(BIN_DIR)/dal_flight_console"
	install -m 0755 daltui/daltui "$(DESTDIR)$(BIN_DIR)/daltui"
	install -m 0755 OBC_GUI/obc_gui "$(DESTDIR)$(BIN_DIR)/obc_gui"
	install -m 0755 EXP_GUI/exp_tui "$(DESTDIR)$(BIN_DIR)/exp_tui"
	install -m 0755 exp_test_gui/exp_test_gui "$(DESTDIR)$(BIN_DIR)/exp_test_gui"
	install -m 0755 usb_cam/usb_cam "$(DESTDIR)$(BIN_DIR)/usb_cam"
	install -m 0755 dal_pdu/dal-pdu-gui "$(DESTDIR)$(BIN_DIR)/dal-pdu-gui"
	install -m 0755 fpga_program/fpga_program "$(DESTDIR)$(BIN_DIR)/fpga_program"
	install -m 0755 spi_flash_s25/spi_flash_s25 "$(DESTDIR)$(BIN_DIR)/spi_flash_s25"
	install -m 0755 spi_eeprom_cat93/spi_eeprom_cat93 "$(DESTDIR)$(BIN_DIR)/spi_eeprom_cat93"
	install -m 0755 samv71_xbld/samv71_xbld "$(DESTDIR)$(BIN_DIR)/samv71_xbld"
	install -m 0755 avr_xbld/avr_xbld "$(DESTDIR)$(BIN_DIR)/avr_xbld"
	$(MAKE) install-clients DESTDIR="$(DESTDIR)" BIN_DIR="$(BIN_DIR)"
	ln -sfn "$(BIN_DIR)/dal" "$(DESTDIR)$(DAL_LINK_DIR)/dal"
	ln -sfn "$(BIN_DIR)/dal_flight_console" "$(DESTDIR)$(DAL_LINK_DIR)/dal_flight_console"
	install -m 0644 bme_service/LICENSE-BOSCH \
	    "$(DESTDIR)/usr/share/licenses/bme-service/LICENSE-BOSCH"
	install -m 0644 bme_service/bme.service "$(DESTDIR)$(UNIT_DIR)/bme.service"
	install -m 0644 temp_service/temp.service "$(DESTDIR)$(UNIT_DIR)/temp.service"
	install -m 0644 hk_service/hk_service.service "$(DESTDIR)$(UNIT_DIR)/hk_service.service"
	install -m 0644 dal_obc_fsw/dal_obc_fsw.service "$(DESTDIR)$(UNIT_DIR)/dal_obc_fsw.service"
	install -d "$(DESTDIR)/etc/default"
	@set -eu; \
	    conf="$(DESTDIR)/etc/default/dal-obc-fsw"; \
	    if [ ! -f "$$conf" ]; then \
	        install -m 0644 dal_obc_fsw/dal-obc-fsw.env "$$conf"; \
	    else \
	        for key in DAL_OBC_VERBOSE DAL_OBC_QUIET_HOUSEKEEPING; do \
	            if grep -q "^$$key=" "$$conf"; then \
	                sed -i "s/^$$key=.*/$$key=1/" "$$conf"; \
	            else \
	                printf '%s=1\n' "$$key" >> "$$conf"; \
	            fi; \
	        done; \
	    fi
	@if [ -z "$(DESTDIR)" ]; then $(MAKE) start; fi
