
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

CFLAGS    := -MD -MP -Wall -Wextra -Wimplicit-function-declaration -std=c23 -I$(INCLUDE_DIR) -I$(LOCAL_WAYLAND_PROTOCOLS_INCLUDE_DIR) -fdiagnostics-color=always
LDFLAGS   := 

CFLAGS    += $(shell ${PKG_CONFIG} --cflags ${PKG_CONFIG_PKGS}) 
LDFLAGS   += $(shell ${PKG_CONFIG} --libs ${PKG_CONFIG_PKGS})

SRC       := src/main.c src/compositor.c src/output.c src/log.c src/client.c src/input.c
OBJS      := $(addprefix $(BUILD_DIR)/, $(SRC:%.c=%.c.o))
DEPS      := $(OBJS:%.o=%.d)

LOCAL_WAYLAND_PROTOCOLS        := $(shell find $(LOCAL_WAYLAND_PROTOCOLS_DIR) -name *.xml)
LOCAL_WAYLAND_PROTOCOL_HEADERS := $(LOCAL_WAYLAND_PROTOCOLS:$(LOCAL_WAYLAND_PROTOCOLS_DIR)/%.xml=$(LOCAL_WAYLAND_PROTOCOLS_INCLUDE_DIR)/%-protocol.h)

all: $(BIN_NAME)

$(BIN_NAME): wayland_protocols .WAIT $(OBJS)
	@$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@
	@echo LD $@

-include $(DEPS)
$(BUILD_DIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) -c $(CFLAGS) -o $@ $<
	@echo CC $<

.PHONY += wayland_protocols
wayland_protocols: $(LOCAL_WAYLAND_PROTOCOL_HEADERS)

$(LOCAL_WAYLAND_PROTOCOLS_INCLUDE_DIR)/%-protocol.h: $(LOCAL_WAYLAND_PROTOCOLS_DIR)/%.xml
	@mkdir -p $(dir $@)
	@$(WAYLAND_SCANNER) server-header $< $@
	@echo WS $<

.PHONY += clean
clean:
	rm -f $(BIN_NAME) $(LOCAL_WAYLAND_PROTOCOL_HEADERS) $(OBJS) $(DEPS)
