# MiniC Toolchain build entry point.
# MiniC 工具链统一构建入口。

SHELL := /bin/sh

PROJECT       := minic-toolchain
MODE          ?= debug
BUILD_DIR     ?= build/$(MODE)
CC            ?= cc
AR            ?= ar

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

MINIC_SOURCES := \
	src/compiler/compiler.c \
	tools/minic/main.c
MINIC_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(MINIC_SOURCES))
MINIC_BINARY  := $(BUILD_DIR)/bin/minic

.PHONY: all help prepare check check-fast check-c0-runtime sanitize \
	bootstrap bootstrap-compare format format-check clean distclean print-config

all: $(MINIC_BINARY)

help:
	@printf '%s\n' \
		"MiniC Toolchain build targets:" \
		"  make                    Build the active MiniC compiler" \
		"  make check-fast         Build and run the C0 compiler gate" \
		"  make check              Run the normal test gate" \
		"  make check-c0-runtime   Use external RISC-V GCC and QEMU when available" \
		"  make sanitize           Run checks with ASan and UBSan" \
		"  make bootstrap          Build the staged bootstrap pipeline" \
		"  make bootstrap-compare  Compare bootstrap stages" \
		"  make format             Format supported source files" \
		"  make format-check       Verify formatting without changes" \
		"  make print-config       Print the active toolchain configuration" \
		"  make clean              Remove the active build directory" \
		"  make distclean          Remove all generated build directories"

prepare:
	@mkdir -p "$(BUILD_DIR)"

$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) -Iinclude $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(MINIC_BINARY): $(MINIC_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(MINIC_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

check-fast: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run.sh

check: check-fast

check-c0-runtime: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
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
		"CPPFLAGS=$(CPPFLAGS)" \
		"MINIC_CFLAGS=$(MINIC_CFLAGS)" \
		"MINIC_LDFLAGS=$(MINIC_LDFLAGS)" \
		"MINIC_BINARY=$(MINIC_BINARY)"

clean:
	@rm -rf "$(BUILD_DIR)"

distclean:
	@rm -rf build

-include $(MINIC_OBJECTS:.o=.d)
