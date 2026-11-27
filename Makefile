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

# Opt-in, pinned GCC libgcc 14.2.0 subset. No extra dependency for regular
# MiniC builds. Archive extraction includes only referenced source members.
RISCV_RUNTIME_CC ?= riscv64-linux-gnu-gcc
RISCV_RUNTIME_AR ?= riscv64-linux-gnu-ar
RISCV_RUNTIME_NM ?= riscv64-linux-gnu-nm
RV64I_RUNTIME_SRC := third_party/gcc-riscv-14.2.0
RV64I_RUNTIME_DIR := $(BUILD_DIR)/runtime/riscv64/rv64i-lp64
RV64I_RUNTIME_ARCHIVE := $(RV64I_RUNTIME_DIR)/libminic-rt.a
RV64I_SOFTFP_SRC := third_party/llvm-compiler-rt-19.1.7
# The manifest is the sole runtime source inventory for all four targets.
RV64I_SOFTFP_SOURCES := $(shell python3 tools/runtime/build_runtime.py --print-sources)
RV64I_SOFTFP_OBJECTS := $(addprefix $(RV64I_RUNTIME_DIR)/softfp/,$(RV64I_SOFTFP_SOURCES:.c=.o))
RV64I_RUNTIME_OBJECTS := \
  $(RV64I_RUNTIME_DIR)/muldi3.o \
  $(RV64I_RUNTIME_DIR)/div.o \
  $(RV64I_SOFTFP_OBJECTS)

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

TARGET_SOURCE_POLICY_SOURCES := \
	src/target/source_policy/data_layout_source.c \
	src/target/source_policy/target_source_info.c \
	src/target/source_policy/language_semantics.c \
	src/target/source_policy/riscv32.c \
	src/target/source_policy/riscv64.c \
	src/target/source_policy/x86_64.c \
	src/target/source_policy/aarch64.c \
	src/target/source_policy/loongarch64.c

TARGET_MACHINE_REGISTER_SOURCES := \
	src/target/riscv/machine_register.c \
	src/target/x86_64/machine_register.c \
	src/target/aarch64/machine_register.c \
	src/target/loongarch64/machine_register.c

MINIC_SOURCES := \
	src/compiler/compiler.c \
	src/compiler/core_function_set.c \
	src/compiler/module_ir_build.c \
	src/compiler/global_payload.c \
	src/compiler/inline_specialize_common.c \
	src/compiler/inline_integer.c \
	src/compiler/inline_symbolic.c \
	src/compiler/inline_boolean.c \
	src/compiler/core_reachability.c \
	src/core/core_ir.c \
	src/compiler/c_lower/core_ir_source.c \
	src/core/core_type.c \
	src/core/module_ir.c \
	src/compiler/c_lower/core_type_import.c \
	src/core/core_ir_verify.c \
	src/core/core_ir_dump.c \
	src/core/value_uses.c \
	src/compiler/c_lower/reentry_index.c \
	src/compiler/c_lower/core_lower.c \
	src/compiler/c_lower/core_lower_expression.c \
	src/compiler/c_lower/core_lower_address.c \
	src/compiler/c_lower/core_lower_scalar_values.c \
	src/compiler/c_lower/core_lower_bit_fields.c \
	src/compiler/c_lower/core_lower_assignments.c \
	src/compiler/c_lower/core_lower_record.c \
	src/compiler/c_lower/core_lower_varargs.c \
	src/compiler/c_lower/core_lower_local_effects.c \
	src/compiler/c_lower/core_lower_loop.c \
	src/compiler/c_lower/core_lower_if.c \
	src/compiler/c_lower/core_lower_switch.c \
	src/compiler/c_lower/core_lower_calls.c \
	src/compiler/c_lower/core_lower_expr_flow.c \
	src/compiler/c_lower/core_lower_expr_scalar.c \
	src/compiler/c_lower/core_lower_arithmetic.c \
	src/compiler/c_lower/core_lower_const_facts.c \
	src/compiler/c_lower/core_const_global_record.c \
	src/compiler/c_lower/cfg_constant_calls.c \
	src/compiler/c_lower/local_facts.c \
	src/compiler/c_lower/source_loop_shapes.c \
	src/compiler/c_lower/core_lower_bool.c \
	src/compiler/c_lower/core_lower_types.c \
	src/compiler/c_lower/core_lower_metadata.c \
	src/compiler/c_lower/core_lower_asm.c \
	src/compiler/c_lower/core_lower_asm_target.c \
	src/compiler/c_lower/core_lower_asm_operands.c \
	src/compiler/c_lower/core_lower_asm_structured.c \
	src/compiler/c_lower/core_lower_asm_scalar_forms.c \
	src/frontend/attribute.c \
	src/frontend/ast.c \
	src/frontend/expression_semantics.c \
	src/frontend/ast_traversal.c \
	src/frontend/function_body.c \
	src/frontend/initializer.c \
	src/frontend/ast_verifier.c \
	src/frontend/cast_normalization.c \
	src/frontend/const_eval.c \
	src/frontend/declaration_sema.c \
	src/frontend/ast_function.c \
	src/frontend/ast_global.c \
	src/frontend/lexer.c \
	src/frontend/gnu_neon_inline_filter.c \
	src/frontend/binary128_literal.c \
	src/frontend/parser_constant.c \
	src/frontend/parser_attribute.c \
	src/frontend/parser_declarator.c \
	src/frontend/parser_enum.c \
	src/frontend/parser_core.c \
	src/frontend/parser_expression.c \
	src/frontend/parser_builtins.c \
	src/frontend/parser_builtin_memory.c \
	src/frontend/parser_function.c \
	src/frontend/parser_function_parameters.c \
	src/frontend/parser_external_object.c \
	src/frontend/parser_pragma.c \
	src/frontend/parser_global.c \
	src/frontend/parser_static_initializer.c \
	src/frontend/parser_global_lookup.c \
	src/frontend/parser_static_constants.c \
	src/frontend/parser_member.c \
	src/frontend/parser_postfix.c \
	src/frontend/parser_record.c \
	src/frontend/parser_static_assert.c \
	src/frontend/parser_statement.c \
	src/frontend/parser_statement_control.c \
	src/frontend/parser_labels.c \
	src/frontend/parser_local_declarations.c \
	src/frontend/parser_gnu_inline_asm.c \
	src/frontend/parser_runtime_initializer.c \
	src/frontend/parser_string.c \
	src/frontend/parser_type.c \
	src/frontend/parser_typedef.c \
	src/frontend/type.c \
	src/target/data_layout.c \
	src/target/target_info.c \
	src/target/target_catalog.c \
	src/target/target_machine_options.c \
	src/target/machine_options/riscv.c \
	src/target/machine_options/loongarch64.c \
	src/target/machine_options/x86_64.c \
	$(TARGET_SOURCE_POLICY_SOURCES) \
	$(TARGET_MACHINE_REGISTER_SOURCES) \
	src/target/backend.c \
	src/target/backend_source.c \
	src/target/common/module_global_emit.c \
	src/target/common/elf_module_symbol_info.c \
	src/target/common/program_visit.c \
	src/target/common/program_emit.c \
	src/target/common/direct_program_emit.c \
	src/target/common/direct_preflight.c \
	src/target/common/direct_function_profile.c \
	src/target/common/direct_function_plan.c \
	src/target/common/core_function_plan.c \
	src/target/common/machine_ir.c \
	src/target/common/machine_builder.c \
	src/target/common/machine_core_coverage.c \
	src/target/common/machine_ir_verify.c \
	src/target/common/machine_gnu_asm.c \
	src/target/common/machine_source_asm_template.c \
	src/target/common/core_function_facts.c \
	src/target/common/core_numeric_shape.c \
	src/target/common/core_value_slots.c \
	src/target/common/core_object_storage.c \
	src/target/common/core_call_view.c \
	src/target/common/core_call_index.c \
	src/target/common/direct_call_plan.c \
	src/target/common/direct_instruction_semantics.c \
	src/target/common/direct_residual_plan.c \
	src/target/common/aggregate_shape.c \
	src/target/common/lp64_aggregate.c \
	src/target/common/lp64_scalar.c \
	src/target/common/lp64_frame.c \
	src/target/riscv/isa_resolve.c \
	src/target/riscv/machine_opcode.c \
	src/target/riscv/machine_gnu_asm.c \
	src/target/riscv/machine_frame_select.c \
	src/target/riscv/machine_core_select.c \
	src/target/riscv64/isa.c \
	src/target/riscv32/rv32_emit.c \
	src/target/riscv32/rv32_frame.c \
	src/target/riscv32/rv32_scalar.c \
	src/target/riscv32/rv32_abi_plan.c \
	src/target/riscv32/rv32_abi_cursor.c \
	src/target/riscv32/rv32_global_emit.c \
	src/target/riscv64/abi.c \
	src/target/riscv64/core_admission.c \
	src/target/riscv64/function_abi_plan.c \
	src/target/riscv64/runtime_libcall.c \
	src/target/riscv64/frame_layout.c \
	src/target/riscv64/asm_plan.c \
	src/target/common/core_spill_layout.c \
	src/target/common/core_object_intervals.c \
	src/target/common/core_object_reuse.c \
	src/target/riscv64/record_snapshot.c \
	src/target/riscv64/legalize.c \
	src/target/riscv64/frame_requirements.c \
	src/target/riscv64/codegen_function.c \
	src/target/riscv64/global_emit.c \
	src/target/x86_64/codegen.c \
	src/target/x86_64/machine_opcode.c \
	src/target/x86_64/machine_gnu_asm.c \
	src/target/x86_64/machine_frame_select.c \
	src/target/x86_64/machine_core_select.c \
	src/target/x86_64/asm_contract.c \
	src/target/x86_64/asm_contract_source.c \
	src/target/x86_64/machine_raid6_source_policy.c \
	src/target/x86_64/abi.c \
	src/target/x86_64/call_plan.c \
	src/target/x86_64/frame.c \
	src/target/x86_64/global_emit.c \
	src/target/aarch64/abi.c \
	src/target/aarch64/machine_opcode.c \
	src/target/aarch64/machine_gnu_asm.c \
	src/target/aarch64/machine_frame_select.c \
	src/target/aarch64/machine_core_select.c \
	src/target/aarch64/machine_source_asm.c \
	src/target/aarch64/machine_sysreg_contract.c \
	src/target/aarch64/machine_lse_source_select.c \
	src/target/aarch64/codegen.c \
	src/target/aarch64/frame_emit.c \
	src/target/aarch64/neon_emit.c \
	src/target/aarch64/asm_contract.c \
	src/target/aarch64/asm_contract_source.c \
	src/target/loongarch64/codegen.c \
	src/target/loongarch64/machine_opcode.c \
	src/target/loongarch64/machine_gnu_asm.c \
	src/target/loongarch64/machine_asm_form.c \
	src/target/loongarch64/machine_select.c \
	src/target/loongarch64/machine_builtin_select.c \
	src/target/loongarch64/machine_llsc_select.c \
	src/target/loongarch64/machine_llsc_extended_select.c \
	src/target/loongarch64/machine_pte_select.c \
	src/target/loongarch64/machine_csr_source_select.c \
	src/target/loongarch64/machine_scalar_source_select.c \
	src/target/loongarch64/machine_uaccess_select.c \
	src/target/loongarch64/machine_source_asm.c \
	src/target/loongarch64/asm_contract_source.c \
	src/target/loongarch64/simd_asm.c \
	src/target/loongarch64/simd_source_policy.c \
	tools/minic-cc/main.c
MINIC_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(MINIC_SOURCES))
MINIC_CC_BINARY := $(BUILD_DIR)/bin/minic-cc
FUNCTION_DIAGNOSTIC_TEST_OBJECT := $(BUILD_DIR)/obj/tests/frontend/function_diagnostic_attribute_test.o
FUNCTION_DIAGNOSTIC_TEST_BINARY := $(BUILD_DIR)/tests/frontend/function-diagnostic-attribute-test
BACKEND_MODULE_ARENA_TEST_OBJECT := $(BUILD_DIR)/obj/tests/target/backend_module_arena_test.o
BACKEND_MODULE_ARENA_TEST_BINARY := $(BUILD_DIR)/tests/target/backend-module-arena-test
DIRECT_LP64_ASM_UNIT_BINARY := $(BUILD_DIR)/tests/compiler/c0/direct-lp64-asm-preflight-unit
MINIC_DRIVER_SOURCES := \
	src/target/target_catalog.c \
	src/target/driver_stage_recipe.c \
	src/target/riscv64/driver_stage_recipe.c \
	tools/minic/driver_cli.c \
	tools/minic/driver_pipeline.c \
	tools/minic/main.c
MINIC_DRIVER_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(MINIC_DRIVER_SOURCES))
# The compiler boundary stays in minic-cc. minic is the user-facing staged
# driver and preserves the historical -S preprocessed-input compatibility path.
MINIC_BINARY  := $(BUILD_DIR)/bin/minic

MINIELF_INCLUDES := -Ielf/include
MINIELF_READER_OBJECT := $(BUILD_DIR)/obj/elf/src/reader.o
MINIELF_REL_WRITER_OBJECT := $(BUILD_DIR)/obj/elf/src/relocatable_writer.o
MINIELF_BINARY_EXPORT_OBJECT := $(BUILD_DIR)/obj/elf/src/binary_export.o
MINIELF_REWRITE_OBJECT := $(BUILD_DIR)/obj/elf/src/rewrite.o
MINIELF_REL_OBJECTS := $(MINIELF_READER_OBJECT) $(MINIELF_REL_WRITER_OBJECT)

MINIAS_INCLUDES := -Iassembler/src $(MINIELF_INCLUDES)
MINIAS_SOURCES := \
	assembler/src/assembler.c \
	assembler/src/expression.c \
	assembler/src/riscv_encode.c \
	assembler/src/string_literal.c \
	assembler/src/elf_writer.c \
	tools/minic-as/main.c
MINIAS_OBJECTS := $(MINIELF_REL_OBJECTS) $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(MINIAS_SOURCES))
MINIAS_BINARY := $(BUILD_DIR)/bin/minic-as

MINIPP_INCLUDES := -Ipreprocessor/include -Ipreprocessor/src -Ipreprocessor/target
MINIPP_SOURCES := \
	preprocessor/src/file_buffer.c \
	preprocessor/src/expression.c \
	preprocessor/src/include.c \
	preprocessor/src/token.c \
	preprocessor/src/preprocessor.c \
	preprocessor/target/riscv_linux_predefines.c \
	tools/minic-cpp/main.c
MINIPP_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(MINIPP_SOURCES))
MINIPP_BINARY := $(BUILD_DIR)/bin/minic-cpp

MINIAR_INCLUDES := -Iarchiver/include $(MINIELF_INCLUDES)
MINIAR_SOURCES := \
	archiver/src/archive.c \
	tools/minic-ar/main.c
MINIAR_OBJECTS := $(MINIELF_READER_OBJECT) $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(MINIAR_SOURCES))
MINIAR_BINARY := $(BUILD_DIR)/bin/minic-ar

MINILD_INCLUDES := -Ilinker/include -Ilinker/src $(MINIELF_INCLUDES)
MINILD_SOURCES := \
	linker/src/linker_script_lex.c \
	linker/src/linker_script_expr.c \
	linker/src/linker_script_parse.c \
	linker/src/linker_script_match.c \
	linker/src/linker.c \
	linker/src/elf32_compat_vdso.c \
	tools/minic-ld/main.c
MINILD_OBJECTS := $(MINIELF_REL_OBJECTS) $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(MINILD_SOURCES))
MINILD_BINARY := $(BUILD_DIR)/bin/minic-ld

MININM_INCLUDES := $(MINIELF_INCLUDES) -Iarchiver/include
MININM_SOURCES := \
	tools/minic-nm/main.c
MININM_OBJECTS := $(MINIELF_READER_OBJECT) $(BUILD_DIR)/obj/archiver/src/archive.o $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(MININM_SOURCES))
MININM_BINARY := $(BUILD_DIR)/bin/minic-nm

MINIOBJCOPY_INCLUDES := $(MINIELF_INCLUDES)
MINIOBJCOPY_SOURCES := \
	tools/minic-objcopy/main.c
MINIOBJCOPY_OBJECTS := $(MINIELF_READER_OBJECT) $(MINIELF_BINARY_EXPORT_OBJECT) $(MINIELF_REWRITE_OBJECT) $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(MINIOBJCOPY_SOURCES))
MINIOBJCOPY_BINARY := $(BUILD_DIR)/bin/minic-objcopy

MINISTRIP_INCLUDES := $(MINIELF_INCLUDES)
MINISTRIP_SOURCES := \
	tools/minic-strip/main.c
MINISTRIP_OBJECTS := $(MINIELF_READER_OBJECT) $(MINIELF_REWRITE_OBJECT) $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(MINISTRIP_SOURCES))
MINISTRIP_BINARY := $(BUILD_DIR)/bin/minic-strip



# OS-style maintenance boundary: production build and a small, explicit gate.
.DEFAULT_GOAL := all
.PHONY: all help prepare clean distclean print-config format format-check \
        check check-fast check-native check-parser check-toolchain \
        check-minias-a0 check-minipp-a0 bootstrap bootstrap-compare \
        runtime-fourarch runtime-fourarch-reference runtime-rv64i
all: $(MINIC_CC_BINARY) $(MINIC_BINARY) $(MINIAS_BINARY) $(MINIPP_BINARY) $(MINIAR_BINARY) $(MINILD_BINARY) $(MININM_BINARY) $(MINIOBJCOPY_BINARY) $(MINISTRIP_BINARY)

help:
	@printf '%s\n' 'make [all]           Build the nine production toolchain binaries' \
	  'make check-fast      Run maintained native matrix and parser rejections' \
	  'make check-toolchain Execute MiniAS/MiniPP native smoke gates' \
	  'make bootstrap       Explicit RV64 B1 self-host fixed-point gate' \
	  'make clean           Remove the selected build directory'



# Real optional target runtime package. It is deliberately not a MiniC core
# dependency and is not a replacement for the complete libgcc multilib.
# Shared third-party C implementations are compiled by the *existing*
# MiniC backends, not copied into architecture-specific code generators.
# Compiler-rt's primitive RV64I libgcc .S remain the explicit seed objects.
runtime-fourarch: $(MINIC_CC_BINARY)
	python3 tools/runtime/build_runtime.py --backend minic --minic "$(MINIC_CC_BINARY)" \
	  --build-dir "$(BUILD_DIR)/runtime-fourarch"

runtime-fourarch-reference:
	python3 tools/runtime/build_runtime.py --backend reference \
	  --build-dir "$(BUILD_DIR)/runtime-fourarch"

runtime-rv64i: $(RV64I_RUNTIME_ARCHIVE)

$(RV64I_RUNTIME_DIR)/%.o: $(RV64I_RUNTIME_SRC)/%.S $(RV64I_RUNTIME_SRC)/riscv-asm.h
	@mkdir -p "$(dir $@)"
	$(RISCV_RUNTIME_CC) -march=rv64i -mabi=lp64 -I"$(RV64I_RUNTIME_SRC)" -c "$<" -o "$@"

# Pinned upstream LLVM compiler-rt soft-float. Always cross-compile for
# RV64I/LP64, never pick the cross toolchain's default LP64D multilib.
$(RV64I_RUNTIME_DIR)/softfp/%.o: $(RV64I_SOFTFP_SRC)/%.c $(wildcard $(RV64I_SOFTFP_SRC)/*.h $(RV64I_SOFTFP_SRC)/*.inc)
	@mkdir -p "$(dir $@)"
	$(RISCV_RUNTIME_CC) -std=gnu11 -O2 -ffreestanding -fno-builtin \
	  -fno-stack-protector -fno-lto -fno-pic -nostdinc \
	  -I"$(RV64I_RUNTIME_SRC)" \
	  -isystem "$(RV64I_SOFTFP_SRC)/freestanding-clang" \
	  -isystem "$(shell $(RISCV_RUNTIME_CC) -print-file-name=include)" \
	  -march=rv64i -mabi=lp64 -I"$(RV64I_SOFTFP_SRC)" -c "$<" -o "$@"

$(RV64I_RUNTIME_ARCHIVE): $(RV64I_RUNTIME_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(RISCV_RUNTIME_AR) rcs "$@" $(RV64I_RUNTIME_OBJECTS)

check-rv64i-runtime-archive: runtime-rv64i
	@symbols="$$($(RISCV_RUNTIME_NM) -g --defined-only $(RV64I_RUNTIME_ARCHIVE))"; \
	for name in __muldi3 __divdi3 __udivdi3 __moddi3 __umoddi3 \
	  __multi3 __cmpti2 __ucmpti2 \
	  __divti3 __udivti3 __modti3 __umodti3 __udivmodti4 \
	  __ashlti3 __ashrti3 __lshrti3 \
	  __addsf3 __subsf3 __mulsf3 __divsf3 \
	  __adddf3 __subdf3 __muldf3 __divdf3 \
	  __clzdi2 __clzsi2 __fe_getround __fe_raise_inexact; do \
	  printf '%s\n' "$$symbols" | awk '{print $$NF}' | grep -Fx "$$name" >/dev/null || { \
	    echo "RV64I_RUNTIME_ARCHIVE=FAIL missing $$name" >&2; exit 1; }; \
	done
	@echo "RV64I_RUNTIME_ARCHIVE=PASS path=$(RV64I_RUNTIME_ARCHIVE) isa=rv64i abi=lp64 symbols=28 integer=16 softfp=8 deps=4"

prepare:
	@mkdir -p "$(BUILD_DIR)"

$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) $(if $(filter release,$(MODE)),-O3,) -MMD -MP -c "$<" -o "$@"

$(MINIC_CC_BINARY): $(MINIC_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(MINIC_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

# The staged driver invokes the sibling minic-cc at runtime. An order-only
# prerequisite prevents parallel `make -j check-fast` from launching it while
# the compiler binary is still being relinked, without relinking the driver
# for every compiler implementation change.
$(MINIC_BINARY): $(MINIC_DRIVER_OBJECTS) | $(MINIC_CC_BINARY)
	@mkdir -p "$(dir $@)"
	$(CC) $(MINIC_DRIVER_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(BUILD_DIR)/obj/assembler/%.o: assembler/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIAS_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD_DIR)/obj/tools/minic-as/%.o: tools/minic-as/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIAS_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(MINIAS_BINARY): $(MINIAS_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(MINIAS_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(BUILD_DIR)/obj/preprocessor/%.o: preprocessor/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIPP_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD_DIR)/obj/tools/minic-cpp/%.o: tools/minic-cpp/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIPP_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(MINIPP_BINARY): $(MINIPP_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(MINIPP_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(BUILD_DIR)/obj/archiver/%.o: archiver/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIAR_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD_DIR)/obj/tools/minic-ar/%.o: tools/minic-ar/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIAR_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(MINIAR_BINARY): $(MINIAR_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(MINIAR_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(BUILD_DIR)/obj/elf/%.o: elf/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIELF_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD_DIR)/obj/linker/%.o: linker/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINILD_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD_DIR)/obj/tools/minic-ld/%.o: tools/minic-ld/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINILD_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(MINILD_BINARY): $(MINILD_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(MINILD_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(BUILD_DIR)/obj/tools/minic-nm/%.o: tools/minic-nm/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MININM_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(MININM_BINARY): $(MININM_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(MININM_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(BUILD_DIR)/obj/tools/minic-objcopy/%.o: tools/minic-objcopy/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIOBJCOPY_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(MINIOBJCOPY_BINARY): $(MINIOBJCOPY_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(MINIOBJCOPY_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(BUILD_DIR)/obj/tools/minic-strip/%.o: tools/minic-strip/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINISTRIP_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(MINISTRIP_BINARY): $(MINISTRIP_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(MINISTRIP_OBJECTS) $(MINIC_LDFLAGS) -o "$@"


# The maintained regression oracle owns its fixtures and execution semantics.
check-native: $(MINIC_CC_BINARY) $(MINIC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" MINIC="$(abspath $(MINIC_BINARY))" HOST_CC="$(CC)" \
	  python3 tests/compiler/c0/native-matrix.py --all

check-parser: $(MINIC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" HOST_CC="$(CC)" \
	  sh tests/compiler/c0/run-parser-rejections.sh --all

check-fast: check-native check-parser
check: check-fast

check-minias-a0: $(MINIAS_BINARY)
	MINIAS="$(abspath $(MINIAS_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	  sh tests/assembler/run-a0.sh

check-minipp-a0: $(MINIPP_BINARY)
	MINIPP="$(abspath $(MINIPP_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	  sh tests/preprocessor/run-a0.sh

check-toolchain: check-minias-a0 check-minipp-a0

bootstrap:
	python3 tools/ci/minic-bootstrap-current-b1-seed-v1.py \
	  --work "$(abspath $(BUILD_DIR))/bootstrap-current-b1"
	python3 tools/ci/minic-bootstrap-current-b1-stage2-v1.py \
	  --work "$(abspath $(BUILD_DIR))/bootstrap-current-b1" --workers 4

bootstrap-compare: bootstrap
	@echo 'BOOTSTRAP_B1=PASS current source Stage1/Stage2 fixed point'

format:
	CLANG_FORMAT="$${CLANG_FORMAT:-clang-format-18}" bash tools/maintenance/run-format.sh write

format-check:
	CLANG_FORMAT="$${CLANG_FORMAT:-clang-format-18}" bash tools/maintenance/run-format.sh check

print-config:
	@printf '%s\n' "MODE=$(MODE)" "BUILD_DIR=$(BUILD_DIR)" "CC=$(CC)" "MINIC_CC_BINARY=$(MINIC_CC_BINARY)"

clean:
	@rm -rf "$(BUILD_DIR)"

distclean:
	@rm -rf build

-include $(MINIC_OBJECTS:.o=.d)
-include $(MINIC_DRIVER_OBJECTS:.o=.d)
-include $(MINIAS_OBJECTS:.o=.d)
-include $(MINIPP_OBJECTS:.o=.d)
-include $(MINIAR_OBJECTS:.o=.d)
-include $(MINILD_OBJECTS:.o=.d)
-include $(MININM_OBJECTS:.o=.d)
-include $(MINIOBJCOPY_OBJECTS:.o=.d)
-include $(MINISTRIP_OBJECTS:.o=.d)
