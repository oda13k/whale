
CC                                  ?= clang
PKG_CONFIG                          ?= pkg-config

BIN_NAME                            ?= whale
BUILD_DIR                           ?= build
INCLUDE_DIR                         := include
LOCAL_WAYLAND_PROTOCOLS_DIR         := wayland_protocols
LOCAL_WAYLAND_PROTOCOLS_INCLUDE_DIR := $(BUILD_DIR)/generated/$(LOCAL_WAYLAND_PROTOCOLS_DIR)

WAYLAND_PROTOCOLS_DIR := $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER       := $(shell $(PKG_CONFIG) --variable=wayland_scanner wayland-scanner)

PKG_CONFIG_PKGS       := wayland-server wlroots-0.19 xkbcommon

BUILD_DEBUG           := 1
BUILD_RELEASE         := 0

EXCLUDE_WARNINGS      :=  -Wno-gnu-statement-expression-from-macro-expansion \
-Wno-gnu-pointer-arith -Wno-declaration-after-statement -Wno-covered-switch-default \
-Wno-pre-c23-compat -Wno-padded -Wno-reserved-identifier -Wno-unsafe-buffer-usage \
-Wno-vla

WARNINGS  := -Wall -Wextra -Wimplicit-function-declaration \
$(EXCLUDE_WARNINGS)

CFLAGS    := -MD -MP -std=c23 $(WARNINGS) \
-I$(INCLUDE_DIR) -I$(LOCAL_WAYLAND_PROTOCOLS_INCLUDE_DIR) \
-fdiagnostics-color=always -D_POSIX_C_SOURCE=200809L -DWLR_USE_UNSTABLE

LDFLAGS   := -lm

ifeq ($(BUILD_DEBUG),1)
	CFLAGS += -ggdb3 -DWHALE_DEBUG=1
	LDFLAGS += -Wl,-export-dynamic
endif

CFLAGS    += $(shell ${PKG_CONFIG} --cflags ${PKG_CONFIG_PKGS})
LDFLAGS   += $(shell ${PKG_CONFIG} --libs ${PKG_CONFIG_PKGS})

SRC       := src/whale.c src/compositor.c src/log.c \
src/debug.c src/output/output.c src/output/scene.c src/output/workspace.c \
src/client/xwayland.c src/client/client.c src/client/surface.c \
src/client/xdg_shell.c src/input/seat.c src/input/pointer.c \
src/input/keyboard.c src/input/keyboard_bindings.c \
src/input/clipboard.c src/utils/toml.c src/utils/proc.c

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
