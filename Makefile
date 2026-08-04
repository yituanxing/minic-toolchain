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

.PHONY: all help prepare check check-fast sanitize bootstrap bootstrap-compare \
	format format-check clean distclean print-config

all: prepare
	@printf '%s\n' "MiniC repository initialized; production C sources are not imported yet."
	@printf '%s\n' "Run 'make help' to list the stable entry points."

help:
	@printf '%s\n' \
		"MiniC Toolchain build targets:" \
		"  make                 Prepare the selected build directory" \
		"  make check-fast      Run the fast pre-commit gate" \
		"  make check           Run the normal test gate" \
		"  make sanitize        Run checks with ASan and UBSan" \
		"  make bootstrap       Build the staged bootstrap pipeline" \
		"  make bootstrap-compare  Compare bootstrap stages" \
		"  make format          Format supported source files" \
		"  make format-check    Verify formatting without changes" \
		"  make print-config    Print the active toolchain configuration" \
		"  make clean           Remove the active build directory" \
		"  make distclean       Remove all generated build directories"

prepare:
	@mkdir -p "$(BUILD_DIR)"
	@printf '%s\n' "$(PROJECT) $(MODE) build directory: $(BUILD_DIR)"

check-fast: prepare
	@printf '%s\n' "check-fast: no production sources imported yet"

check: check-fast
	@printf '%s\n' "check: no extended test suites imported yet"

sanitize:
	@$(MAKE) MODE=sanitize check

bootstrap: prepare
	@printf '%s\n' "bootstrap: pending import of the validated Python oracle and C implementation"

bootstrap-compare: bootstrap
	@printf '%s\n' "bootstrap-compare: pending staged compiler outputs"

format:
	@printf '%s\n' "format: formatter policy will be added with the first production C source"

format-check:
	@printf '%s\n' "format-check: formatter policy will be added with the first production C source"

print-config:
	@printf '%s\n' \
		"PROJECT=$(PROJECT)" \
		"MODE=$(MODE)" \
		"BUILD_DIR=$(BUILD_DIR)" \
		"CC=$(CC)" \
		"AR=$(AR)" \
		"CPPFLAGS=$(CPPFLAGS)" \
		"MINIC_CFLAGS=$(MINIC_CFLAGS)" \
		"MINIC_LDFLAGS=$(MINIC_LDFLAGS)"

clean:
	@rm -rf "$(BUILD_DIR)"

distclean:
	@rm -rf build
