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
RISCV_OBJDUMP ?=
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
	src/frontend/attribute.c \
	src/frontend/ast.c \
	src/frontend/ast_verifier.c \
	src/frontend/cast_normalization.c \
	src/frontend/ast_function.c \
	src/frontend/ast_global.c \
	src/frontend/lexer.c \
	src/frontend/parser_constant.c \
	src/frontend/parser_attribute.c \
	src/frontend/parser_declarator.c \
	src/frontend/parser_core.c \
	src/frontend/parser_expression.c \
	src/frontend/parser_function.c \
	src/frontend/parser_global.c \
	src/frontend/parser_member.c \
	src/frontend/parser_postfix.c \
	src/frontend/parser_record.c \
	src/frontend/parser_static_assert.c \
	src/frontend/parser_statement.c \
	src/frontend/parser_string.c \
	src/frontend/parser_type.c \
	src/frontend/parser_typedef.c \
	src/frontend/token.c \
	src/frontend/type.c \
	src/target/riscv64/layout.c \
	src/target/riscv64/codegen_support.c \
	src/target/riscv64/codegen_expression.c \
	src/target/riscv64/codegen_inline_asm.c \
	src/target/riscv64/codegen_statement.c \
	src/target/riscv64/codegen_function.c \
	tools/minic/main.c
MINIC_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(MINIC_SOURCES))
MINIC_BINARY  := $(BUILD_DIR)/bin/minic

TOKEN_MODEL_TEST_SOURCES := \
	src/frontend/token.c \
	tests/frontend/token_model_test.c
TOKEN_MODEL_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(TOKEN_MODEL_TEST_SOURCES))
TOKEN_MODEL_TEST_BINARY  := $(BUILD_DIR)/tests/frontend/token-model-test

LEXER_TEST_SOURCES := \
	src/frontend/lexer.c \
	src/frontend/token.c \
	tests/frontend/lexer_test.c
LEXER_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(LEXER_TEST_SOURCES))
LEXER_TEST_BINARY  := $(BUILD_DIR)/tests/frontend/lexer-test

TYPE_TEST_SOURCES := \
	src/frontend/type.c \
	tests/frontend/type_test.c
TYPE_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(TYPE_TEST_SOURCES))
TYPE_TEST_BINARY  := $(BUILD_DIR)/tests/frontend/type-test

RECORD_TEST_SOURCES := \
	src/frontend/ast.c \
	src/frontend/type.c \
	tests/frontend/record_test.c
RECORD_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(RECORD_TEST_SOURCES))
RECORD_TEST_BINARY  := $(BUILD_DIR)/tests/frontend/record-test

TYPE_ALIAS_TEST_SOURCES := \
	src/frontend/ast.c \
	src/frontend/type.c \
	tests/frontend/type_alias_test.c
TYPE_ALIAS_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(TYPE_ALIAS_TEST_SOURCES))
TYPE_ALIAS_TEST_BINARY  := $(BUILD_DIR)/tests/frontend/type-alias-test

AST_CONTRACT_TEST_SOURCES := \
	src/frontend/ast.c \
	src/frontend/ast_global.c \
	src/frontend/ast_verifier.c \
	src/frontend/cast_normalization.c \
	src/frontend/type.c \
	tests/frontend/ast_contract_test.c
AST_CONTRACT_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(AST_CONTRACT_TEST_SOURCES))
AST_CONTRACT_TEST_BINARY  := $(BUILD_DIR)/tests/frontend/ast-contract-test

LAYOUT_TEST_SOURCES := \
	src/frontend/ast.c \
	src/frontend/type.c \
	src/target/riscv64/layout.c \
	tests/target/riscv64/layout_test.c
LAYOUT_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(LAYOUT_TEST_SOURCES))
LAYOUT_TEST_BINARY  := $(BUILD_DIR)/tests/target/riscv64/layout-test

.PHONY: all help prepare check check-fast check-token-model check-lexer \
	check-type check-record check-type-alias check-ast-contract check-layout \
	check-static-functions \
	check-unsigned-declarations check-long-types check-for-loops check-unbounded-for-break \
	check-prefix-decrement-update check-cast-expressions \
	check-unsigned-char-layout check-pointer-subscripts check-pointer-arithmetic \
	check-pointer-object-const check-const-locals check-global-objects check-bitwise-xor \
	check-integer-bit-operations check-pointer-members \
	check-expression-statements check-postfix-subscripts \
	check-compound-xor-assignment check-c0-runtime check-programs-c0 \
	check-runtime sanitize bootstrap bootstrap-compare format format-check \
	clean distclean print-config

all: $(MINIC_BINARY)

help:
	@printf '%s\n' \
		"MiniC Toolchain build targets:" \
		"  make                    Build the active MiniC compiler" \
		"  make check-fast         Run the fast frontend and C0 gates" \
		"  make check-token-model  Run the token data-model unit gate" \
		"  make check-lexer        Run the C0 lexer unit gate" \
		"  make check-type         Run the frontend type-value unit gate" \
		"  make check-record       Run the record ownership unit gate" \
		"  make check-type-alias   Run recursive array and typedef ownership gates" \
		"  make check-ast-contract Run parsed/normalized AST contract gates" \
		"  make check-layout       Run the RV64 object-layout unit gate" \
		"  make check-static-functions Run internal-linkage and typed-return gates" \
		"  make check-unsigned-declarations Run unsigned declaration-list gates" \
		"  make check-long-types Run signed/unsigned long declaration gates" \
		"  make check-for-loops    Run for-loop lowering and boundary gates" \
		"  make check-unbounded-for-break Run empty-condition for and break gates" \
		"  make check-prefix-decrement-update Run --local for-update gates" \
		"  make check-cast-expressions Run bounded cast and typedef-shadow gates" \
		"  make check-unsigned-char-layout Run byte layout, access, and promotion gates" \
		"  make check-pointer-subscripts Run pointer subscript read/write gates" \
		"  make check-pointer-arithmetic Run complete-object pointer scaling gates" \
		"  make check-pointer-object-const Run per-pointer const qualifier gates" \
		"  make check-const-locals Run const local initialization and mutability gates" \
		"  make check-global-objects Run global array lookup and shadowing gates" \
		"  make check-bitwise-xor  Run XOR precedence, type, and lowering gates" \
		"  make check-integer-bit-operations Run shift/AND precedence, type, and lowering gates" \
		"  make check-pointer-members Run pointer record member and field gates" \
		"  make check-expression-statements Run expression/assignment statement gates" \
		"  make check-postfix-subscripts Run repeatable multidimensional subscript gates" \
		"  make check-compound-xor-assignment Run ^= single-evaluation and type gates" \
		"  make check              Run the normal host-side test gate" \
		"  make check-c0-runtime   Run focused RISC-V/QEMU microprogram gates" \
		"  make check-programs-c0  Differentially compare real programs: GCC vs MiniC" \
		"  make check-runtime      Run all available target runtime gates" \
		"  make sanitize           Run host checks with ASan and UBSan" \
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

$(LEXER_TEST_BINARY): $(LEXER_TEST_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(LEXER_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(TYPE_TEST_BINARY): $(TYPE_TEST_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(TYPE_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(RECORD_TEST_BINARY): $(RECORD_TEST_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(RECORD_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(TYPE_ALIAS_TEST_BINARY): $(TYPE_ALIAS_TEST_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(TYPE_ALIAS_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(AST_CONTRACT_TEST_BINARY): $(AST_CONTRACT_TEST_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(AST_CONTRACT_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(LAYOUT_TEST_BINARY): $(LAYOUT_TEST_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(LAYOUT_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

check-token-model: $(TOKEN_MODEL_TEST_BINARY)
	"$(abspath $(TOKEN_MODEL_TEST_BINARY))"

check-lexer: $(LEXER_TEST_BINARY)
	"$(abspath $(LEXER_TEST_BINARY))"

check-type: $(TYPE_TEST_BINARY)
	"$(abspath $(TYPE_TEST_BINARY))"

check-record: $(RECORD_TEST_BINARY)
	"$(abspath $(RECORD_TEST_BINARY))"

check-type-alias: $(TYPE_ALIAS_TEST_BINARY)
	"$(abspath $(TYPE_ALIAS_TEST_BINARY))"

check-ast-contract: $(AST_CONTRACT_TEST_BINARY)
	"$(abspath $(AST_CONTRACT_TEST_BINARY))"

check-layout: $(LAYOUT_TEST_BINARY)
	"$(abspath $(LAYOUT_TEST_BINARY))"

check-static-functions: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-static-functions.sh

check-unsigned-declarations: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-unsigned-declarations.sh

check-long-types: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-long-types.sh

check-for-loops: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-for-loops.sh

check-unbounded-for-break: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-unbounded-for-break.sh

check-prefix-decrement-update: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-prefix-decrement-update.sh

check-cast-expressions: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-cast-expressions.sh

check-unsigned-char-layout: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-unsigned-char-layout.sh

check-pointer-subscripts: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-pointer-subscripts.sh

check-pointer-arithmetic: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-pointer-arithmetic.sh

check-pointer-object-const: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-pointer-object-const.sh

check-const-locals: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-const-locals.sh

check-global-objects: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-global-objects.sh

check-bitwise-xor: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-bitwise-xor.sh

check-integer-bit-operations: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-integer-bit-operations.sh

check-pointer-members: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-pointer-members.sh

check-expression-statements: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-expression-statements.sh

check-postfix-subscripts: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-postfix-subscripts.sh

check-compound-xor-assignment: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run-compound-xor-assignment.sh

check-fast: check-token-model check-lexer check-type check-record check-type-alias check-ast-contract check-layout check-static-functions check-unsigned-declarations check-long-types check-for-loops check-unbounded-for-break check-prefix-decrement-update check-cast-expressions check-unsigned-char-layout check-pointer-subscripts check-pointer-arithmetic check-pointer-object-const check-const-locals check-global-objects check-bitwise-xor check-integer-bit-operations check-pointer-members check-expression-statements check-postfix-subscripts check-compound-xor-assignment $(MINIC_BINARY)
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

check-programs-c0: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	RISCV_CC="$(RISCV_CC)" \
	RISCV_OBJDUMP="$(RISCV_OBJDUMP)" \
	QEMU_RISCV64="$(QEMU_RISCV64)" \
	REQUIRE_RISCV_RUNTIME="$(REQUIRE_RISCV_RUNTIME)" \
	sh tests/programs/c0/run.sh

check-runtime: check-c0-runtime check-programs-c0

sanitize:
	@$(MAKE) MODE=sanitize check

bootstrap: $(MINIC_BINARY)
	@printf '%s\n' "bootstrap: deferred until the compiler capability ladder reaches its source profile"

bootstrap-compare: bootstrap
	@printf '%s\n' "bootstrap-compare: no bootstrap stages exist yet"

format:
	CLANG_FORMAT="$${CLANG_FORMAT:-clang-format-18}" \
		bash tools/maintenance/run-format.sh write

format-check:
	CLANG_FORMAT="$${CLANG_FORMAT:-clang-format-18}" \
		bash tools/maintenance/run-format.sh check

print-config:
	@printf '%s\n' \
		"PROJECT=$(PROJECT)" \
		"MODE=$(MODE)" \
		"BUILD_DIR=$(BUILD_DIR)" \
		"CC=$(CC)" \
		"AR=$(AR)" \
		"RISCV_CC=$(RISCV_CC)" \
		"RISCV_OBJDUMP=$(RISCV_OBJDUMP)" \
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
-include $(LEXER_TEST_OBJECTS:.o=.d)
-include $(TYPE_TEST_OBJECTS:.o=.d)
-include $(RECORD_TEST_OBJECTS:.o=.d)
-include $(TYPE_ALIAS_TEST_OBJECTS:.o=.d)
-include $(AST_CONTRACT_TEST_OBJECTS:.o=.d)
-include $(LAYOUT_TEST_OBJECTS:.o=.d)
