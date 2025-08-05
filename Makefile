
CC                                  := clang
PKG_CONFIG                          ?= pkg-config

BIN_NAME                            ?= whale
BUILD_DIR                           ?= build
INCLUDE_DIR                         := include
LOCAL_WAYLAND_PROTOCOLS_DIR         := wayland_protocols
LOCAL_WAYLAND_PROTOCOLS_INCLUDE_DIR := $(BUILD_DIR)/generated/$(LOCAL_WAYLAND_PROTOCOLS_DIR)

WAYLAND_PROTOCOLS_DIR = $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER       = $(shell $(PKG_CONFIG) --variable=wayland_scanner wayland-scanner)

PKG_CONFIG_PKGS       = wayland-server wlroots-0.19 xkbcommon

TARGET    := debug

CFLAGS    := -MD -MP -Wall -Wextra -Wimplicit-function-declaration -std=c23 -I$(INCLUDE_DIR) -I$(LOCAL_WAYLAND_PROTOCOLS_INCLUDE_DIR) -fdiagnostics-color=always -D_POSIX_C_SOURCE=200809L
LDFLAGS   := -lm

ifeq ($(TARGET),debug)
	CFLAGS += -ggdb3 -DWHALE_TARGET=debug
endif

BROKEN_CODE_C23_LSP := 1
ifeq ($(BROKEN_CODE_C23_LSP),1)
	CFLAGS += -Dtrue=1 -Dfalse=0
endif

CFLAGS    += $(shell ${PKG_CONFIG} --cflags ${PKG_CONFIG_PKGS}) 
LDFLAGS   += $(shell ${PKG_CONFIG} --libs ${PKG_CONFIG_PKGS})

SRC       := src/main.c src/output.c src/log.c \
src/input.c src/input/keyboard.c src/utils.c src/window/xdg.c \
src/window/client.c src/window/surface.c

OBJS      := $(addprefix $(BUILD_DIR)/, $(SRC:%.c=%.c.o))
DEPS      := $(OBJS:%.o=%.d)

LOCAL_WAYLAND_PROTOCOLS        := $(shell find $(LOCAL_WAYLAND_PROTOCOLS_DIR) -name *.xml)
LOCAL_WAYLAND_PROTOCOL_HEADERS := $(LOCAL_WAYLAND_PROTOCOLS:$(LOCAL_WAYLAND_PROTOCOLS_DIR)/%.xml=$(LOCAL_WAYLAND_PROTOCOLS_INCLUDE_DIR)/%-protocol.h)

COMMON_DEPS := Makefile

all: $(BIN_NAME)

$(BIN_NAME): wayland_protocols .WAIT $(OBJS) $(COMMON_DEPS)
	@$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@
	@echo LD $@

-include $(DEPS)
$(BUILD_DIR)/%.c.o: %.c $(COMMON_DEPS)
	@mkdir -p $(dir $@)
	@$(CC) -c $(CFLAGS) -o $@ $<
	@echo CC $<

.PHONY += wayland_protocols
wayland_protocols: $(LOCAL_WAYLAND_PROTOCOL_HEADERS)

$(LOCAL_WAYLAND_PROTOCOLS_INCLUDE_DIR)/%-protocol.h: $(LOCAL_WAYLAND_PROTOCOLS_DIR)/%.xml $(COMMON_DEPS)
	@mkdir -p $(dir $@)
	@$(WAYLAND_SCANNER) server-header $< $@
	@echo WS $<

.PHONY += clean
clean:
	rm -f $(BIN_NAME) $(LOCAL_WAYLAND_PROTOCOL_HEADERS) $(OBJS) $(DEPS)
