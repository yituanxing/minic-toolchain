# MiniC Toolchain build entry point.
# MiniC 工具链统一构建入口。

SHELL := /bin/sh

PROJECT       := minic-toolchain
MODE          ?= debug
BUILD_DIR     ?= build/$(MODE)
CC            ?= cc
AR            ?= ar
RISCV_CC      ?= riscv64-buildroot-linux-musl-gcc
QEMU_RISCV64  ?= qemu-riscv64
REQUIRE_RISCV_RUNTIME ?= 0

CPPFLAGS      ?=
CFLAGS        ?=
LDFLAGS       ?=

COMMON_WARNINGS := \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Wconversion \
	-Wshadow \
	-Wstrict-prototypes \
	-Wmissing-prototypes

ifeq ($(MODE),debug)
MODE_CFLAGS := -O0 -g3
else ifeq ($(MODE),release)
MODE_CFLAGS := -O2 -DNDEBUG
else ifeq ($(MODE),sanitize)
MODE_CFLAGS := -O1 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
MODE_LDFLAGS := -fsanitize=address,undefined
else
$(error Unsupported MODE '$(MODE)'; expected debug, release, or sanitize)
endif

MINIC_CFLAGS  := -std=c11 $(COMMON_WARNINGS) $(MODE_CFLAGS) $(CFLAGS)
MINIC_LDFLAGS := $(MODE_LDFLAGS) $(LDFLAGS)
MINIC_INCLUDES := -Iinclude -Isrc

MINIC_SOURCES := \
	src/compiler/compiler.c \
	src/frontend/token.c \
	tools/minic/main.c
MINIC_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(MINIC_SOURCES))
MINIC_BINARY  := $(BUILD_DIR)/bin/minic

TOKEN_MODEL_TEST_SOURCES := \
	src/frontend/token.c \
	tests/frontend/token_model_test.c
TOKEN_MODEL_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(TOKEN_MODEL_TEST_SOURCES))
TOKEN_MODEL_TEST_BINARY  := $(BUILD_DIR)/tests/frontend/token-model-test

.PHONY: all help prepare check check-fast check-token-model check-c0-runtime \
	sanitize bootstrap bootstrap-compare format format-check clean distclean print-config

all: $(MINIC_BINARY)

help:
	@printf '%s\n' \
		"MiniC Toolchain build targets:" \
		"  make                    Build the active MiniC compiler" \
		"  make check-fast         Run the fast frontend and C0 gates" \
		"  make check-token-model  Run the token data-model unit gate" \
		"  make check              Run the normal test gate" \
		"  make check-c0-runtime   Use external RISC-V GCC and QEMU when available" \
		"  make sanitize           Run checks with ASan and UBSan" \
		"  make print-config       Print the active toolchain configuration" \
		"  make clean              Remove the active build directory" \
		"  make distclean          Remove all generated build directories"

prepare:
	@mkdir -p "$(BUILD_DIR)"

$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(MINIC_BINARY): $(MINIC_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(MINIC_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(TOKEN_MODEL_TEST_BINARY): $(TOKEN_MODEL_TEST_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(TOKEN_MODEL_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

check-token-model: $(TOKEN_MODEL_TEST_BINARY)
	"$(abspath $(TOKEN_MODEL_TEST_BINARY))"

check-fast: check-token-model $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run.sh

check: check-fast

check-c0-runtime: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	RISCV_CC="$(RISCV_CC)" \
	QEMU_RISCV64="$(QEMU_RISCV64)" \
	REQUIRE_RISCV_RUNTIME="$(REQUIRE_RISCV_RUNTIME)" \
	sh tests/compiler/c0/run-runtime.sh

sanitize:
	@$(MAKE) MODE=sanitize check

bootstrap: $(MINIC_BINARY)
	@printf '%s\n' "bootstrap: deferred until the compiler capability ladder reaches its source profile"

bootstrap-compare: bootstrap
	@printf '%s\n' "bootstrap-compare: no bootstrap stages exist yet"

format:
	@printf '%s\n' "format: formatter policy is not automated yet"

format-check:
	@printf '%s\n' "format-check: formatter policy is not automated yet"

print-config:
	@printf '%s\n' \
		"PROJECT=$(PROJECT)" \
		"MODE=$(MODE)" \
		"BUILD_DIR=$(BUILD_DIR)" \
		"CC=$(CC)" \
		"AR=$(AR)" \
		"RISCV_CC=$(RISCV_CC)" \
		"QEMU_RISCV64=$(QEMU_RISCV64)" \
		"REQUIRE_RISCV_RUNTIME=$(REQUIRE_RISCV_RUNTIME)" \
		"CPPFLAGS=$(CPPFLAGS)" \
		"MINIC_CFLAGS=$(MINIC_CFLAGS)" \
		"MINIC_LDFLAGS=$(MINIC_LDFLAGS)" \
		"MINIC_BINARY=$(MINIC_BINARY)"

clean:
	@rm -rf "$(BUILD_DIR)"

distclean:
	@rm -rf build

-include $(MINIC_OBJECTS:.o=.d)
-include $(TOKEN_MODEL_TEST_OBJECTS:.o=.d)
