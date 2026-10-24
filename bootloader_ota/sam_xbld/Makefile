# SAMV71 OTA client. Run make from this directory.

CC      ?= cc
TARGET  := samv71_xbld

BUILD   ?= build
SRCD    := src
LIBD    := lib
DEPLOY_DIR ?= ../../deploy/samv71_xbld

# -- Sources -----------------------------------------------------
APP_SRCS := \
    $(SRCD)/main.c \
    $(SRCD)/app/app_args.c \
    $(SRCD)/app/xbld_client.c \
    $(SRCD)/app/xbld_state.c \
    $(SRCD)/app/hex_reader.c \
    $(SRCD)/app/xcli_codec.c \
    $(SRCD)/app/fw_catalog.c

XTP_SRCS := \
    $(LIBD)/xtp/xtp.c \
    $(LIBD)/xtp/xtp_arq.c \
    $(LIBD)/xtp/xtp_crc.c \
    $(LIBD)/xtp/xtp_crc_table.c \
    $(LIBD)/xtp/xtp_ecc.c \
    $(LIBD)/xtp/xtp_hmac.c \
    $(LIBD)/xtp/xtp_stats.c \
    $(LIBD)/xtp/xtp_port.c \
    $(LIBD)/xtp/xtp_seg.c

LINENOISE_SRCS := \
    $(LIBD)/linenoise/linenoise.c

ALL_SRCS := $(APP_SRCS) $(XTP_SRCS) $(LINENOISE_SRCS)
OBJS     := $(patsubst %.c,$(BUILD)/%.o,$(ALL_SRCS))

# -- Flags -----------------------------------------------------
CFLAGS  += -Wall -Wextra -Wno-unused-parameter \
           -Iinclude \
           -I$(LIBD)/xtp \
           -I$(LIBD)/linenoise \
           -I$(SRCD)/app \
           -std=c99 \
           -D_GNU_SOURCE \
           -O2 -pthread

LDLIBS  += -pthread

ifeq ($(DEBUG),1)
CFLAGS  += -g -O0 -DDEBUG -fsanitize=address,undefined
LDFLAGS += -fsanitize=address,undefined
endif

# -- Rules -----------------------------------------------------
.PHONY: all debug check target obcv110 deploy clean

all: $(BUILD)/$(TARGET)

debug:
	$(MAKE) BUILD=build-debug DEBUG=1 all

# Source the OBC Yocto SDK in a child shell so the host build environment is
# unaffected. Use a separate output tree to prevent mixing x86 and ARM objects.
target obcv110:
	./build-obcv110.sh all

deploy: obcv110
	install -d "$(DEPLOY_DIR)"
	install -m 0755 build-obcv110/$(TARGET) "$(DEPLOY_DIR)/$(TARGET)"

$(BUILD)/$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
	@echo "Built: $@"

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

check: all $(BUILD)/test_fw_slot
	$(BUILD)/$(TARGET) --help >/dev/null
	$(BUILD)/test_fw_slot

$(BUILD)/test_fw_slot: tests/test_fw_slot.c $(SRCD)/app/fw_catalog.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf build build-debug build-obcv110
