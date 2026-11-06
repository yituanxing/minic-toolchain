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
	src/target/riscv32/machine_register.c \
	src/target/riscv64/machine_register.c \
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
	src/frontend/token.c \
	src/frontend/type.c \
	src/target/data_layout.c \
	src/target/target_info.c \
	src/target/target_catalog.c \
	src/target/target_machine_options.c \
	src/target/machine_options/riscv.c \
	src/target/machine_options/aarch64.c \
	src/target/machine_options/loongarch64.c \
	src/target/machine_options/x86_64.c \
	$(TARGET_SOURCE_POLICY_SOURCES) \
	$(TARGET_MACHINE_REGISTER_SOURCES) \
	src/target/backend.c \
	src/target/backend_source.c \
	src/target/backend_core_usage.c \
	src/target/common/global_emit.c \
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

SEMANTIC_SNAPSHOT_TEST_SOURCES := \
	src/frontend/ast.c \
	src/frontend/type.c \
	tests/frontend/semantic_snapshot_test.c
SEMANTIC_SNAPSHOT_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(SEMANTIC_SNAPSHOT_TEST_SOURCES))
SEMANTIC_SNAPSHOT_TEST_BINARY := $(BUILD_DIR)/tests/frontend/semantic-snapshot-test

TYPE_ALIAS_TEST_SOURCES := \
	src/frontend/ast.c \
	src/frontend/type.c \
	tests/frontend/type_alias_test.c
TYPE_ALIAS_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(TYPE_ALIAS_TEST_SOURCES))
TYPE_ALIAS_TEST_BINARY  := $(BUILD_DIR)/tests/frontend/type-alias-test

AST_CONTRACT_TEST_SOURCES := \
	src/frontend/ast.c \
	src/frontend/expression_semantics.c \
	src/frontend/const_eval.c \
	src/frontend/ast_global.c \
	src/frontend/ast_traversal.c \
	src/frontend/ast_verifier.c \
	src/frontend/cast_normalization.c \
	src/frontend/type.c \
	src/target/data_layout.c \
	src/target/target_info.c \
	src/target/target_catalog.c \
	$(TARGET_SOURCE_POLICY_SOURCES) \
	$(TARGET_MACHINE_REGISTER_SOURCES) \
	tests/frontend/ast_contract_test.c
AST_CONTRACT_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(AST_CONTRACT_TEST_SOURCES))
AST_CONTRACT_TEST_BINARY  := $(BUILD_DIR)/tests/frontend/ast-contract-test

LAYOUT_TEST_SOURCES := \
	src/frontend/ast.c \
	src/frontend/type.c \
	src/target/data_layout.c \
	src/target/target_info.c \
	src/target/target_catalog.c \
	$(TARGET_SOURCE_POLICY_SOURCES) \
	$(TARGET_MACHINE_REGISTER_SOURCES) \
	tests/target/riscv64/layout_test.c
LAYOUT_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(LAYOUT_TEST_SOURCES))
LAYOUT_TEST_BINARY  := $(BUILD_DIR)/tests/target/riscv64/layout-test

RV64_ABI_TEST_SOURCES := \
	src/core/core_type.c \
	src/target/common/aggregate_shape.c \
	src/target/riscv64/abi.c \
	src/target/riscv64/function_abi_plan.c \
	tests/target/riscv64/abi_test.c
RV64_ABI_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(RV64_ABI_TEST_SOURCES))
RV64_ABI_TEST_BINARY  := $(BUILD_DIR)/tests/target/riscv64/abi-test

.PHONY: runtime-fourarch runtime-fourarch-reference runtime-rv64i check-rv64i-runtime-archive all help prepare check check-fast check-four-backend-lua-frontier check-four-backend-lua-accept check-four-backend-zlib-frontier check-four-backend-zlib-accept check-target-selection-m0 check-four-backend-m39-lp64-bitops check-four-backend-m15-scalar-return check-four-backend-m14-shift check-four-backend-m13-narrow-arith check-four-backend-m12-bitwise check-four-backend-m11-index check-four-backend-m10-mod check-four-backend-m9-unsigned check-four-backend-m8-narrow check-four-backend-m7-frame check-four-backend-m6-address check-four-backend-m5-pointer check-four-backend-m4-global check-four-backend-m38-fp-varargs check-four-backend-m37-fp-arith check-four-backend-m36-lp64-mul check-four-backend-m35-ptr-to-lp64 check-four-backend-m34-fp-call-result check-four-backend-m33-pointer-less check-four-backend-m32-unreachable check-four-backend-m31-float-promote check-four-backend-m30-fp-call check-four-backend-m29-double-cmp check-four-backend-m28-int-double check-four-backend-m27-fp-parameter check-four-backend-m26-double-div check-four-backend-m25-double-transport check-four-backend-m24-indirect-call check-four-backend-m23-lp64-compare check-four-backend-m22-lp64-conv check-four-backend-m21-lp64-add check-four-backend-m20-lp64-abi check-four-backend-m19-ptr-compare check-four-backend-m18-varargs check-four-backend-m17-record check-four-backend-cjson-accept check-four-backend-cjson-frontier check-four-backend-tinyaes-accept check-four-backend-tinyaes-frontier check-four-backend-m3-call check-four-backend-m2-stack check-four-backend-m2 check-four-backend-m1 check-four-target-model-m0 check-x86_64-busybox-all-eligible check-x86_64-busybox-complex-five check-x86_64-busybox-host-link check-elf-constructors check-x86_64-one-gp-sysv check-x86_64-busybox-allyes-discovery check-x86_64-busybox-12 check-x86_64-sqlite-frontier check-x86_64-m1 check-x86_64-tinyaes check-x86_64-cjson check-x86_64-abi-elf check-x86_64-lua-frontier check-x86_64-zlib-frontier check-minias-a0 check-minipp-a0 check-minielf-reader-a0 check-mininm-a0 check-mininm-a1 check-miniobjcopy-a0 check-miniobjcopy-a1 check-minicstrip-a0 check-miniar-a0 check-miniar-a1 check-minild-a0 check-minild-a1 check-minild-a2 check-minild-a3 check-minild-a4 check-minild-a5 check-minild-a6 check-minild-script-a0 check-minild-script-a1 check-token-model check-lexer \
	check-type check-record check-semantic-snapshot check-type-alias check-ast-contract check-layout check-rv64-abi \
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

all: $(MINIC_CC_BINARY) $(MINIC_BINARY) $(MINIAS_BINARY) $(MINIPP_BINARY) $(MINIAR_BINARY) $(MINILD_BINARY) $(MININM_BINARY) $(MINIOBJCOPY_BINARY) $(MINISTRIP_BINARY)

help:
	@printf '%s\n' \
		"MiniC Toolchain build targets:" \
		"  make                    Build minic driver plus minic-cc/cpp/as/ar/ld/binutils tools" \
		"  make check-minias-a0    Run the initial .s -> ELF ET_REL MiniAS gate" \
		"  make check-minipp-a0    Compare independent MiniPP output byte-for-byte with GCC" \
		"  make check-minielf-reader-a0 Validate shared ELF reader on GNU-as RV32/RV64 objects" \
		"  make check-mininm-a0    Differentially validate minic-nm against GNU nm" \
		"  make check-mininm-a1    Differentially validate regular/thin archive nm" \
		"  make check-miniobjcopy-a0 Differentially validate ELF -> binary export" \
		"  make check-miniobjcopy-a1 Differentially validate ELF rewrite (-G/-S/-R)" \
		"  make check-minicstrip-a0 Differentially validate minic-strip against GNU strip" \
		"  make check-miniar-a0    Compare MiniAR archives byte-for-byte with GNU ar" \
		"  make check-miniar-a1    Check thin flattening and archive listing" \
		"  make check-minild-a0    Validate ELF64 RISC-V relocatable linking against GNU ld" \
		"  make check-minild-a1    Validate thin archive whole/group semantics" \
		"  make check-minild-a2    Validate static ET_EXEC and QEMU runtime" \
		"  make check-minild-a3    Validate regular archive malloc/free runtime" \
		"  make check-minild-a4    Validate minimal ET_DYN dynamic metadata" \
		"  make check-minild-a5    Validate PLT/GOT/JUMP_SLOT shared runtime" \
		"  make check-minild-a6    Validate Global-Dynamic TLS shared runtime" \
		"  make check-minild-script-a0 Parse the Linux linker-script subset" \
		"  make check-minild-script-a1 Validate script-directed ET_EXEC symbol semantics" \
		"  make check-fast         Run the fast frontend and C0 gates" \
		"  make check-rv32-wide-branch-condition Compare RV32 64-bit branch truth under GNU/MiniC QEMU" \
		"  make check-token-model  Run the token data-model unit gate" \
		"  make check-lexer        Run the C0 lexer unit gate" \
		"  make check-type         Run the frontend type-value unit gate" \
		"  make check-record       Run the record ownership unit gate" \
		"  make check-type-alias   Run recursive array and typedef ownership gates" \
		"  make check-ast-contract Run parsed/normalized AST contract gates" \
		"  make check-layout       Run the RV64 object-layout unit gate" \
		"  make check-rv64-abi     Run the canonical RV64 ABI classification/placement gate" \
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
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(MINIC_CC_BINARY): $(MINIC_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(MINIC_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(FUNCTION_DIAGNOSTIC_TEST_BINARY): $(filter-out $(BUILD_DIR)/obj/tools/minic-cc/main.o,$(MINIC_OBJECTS)) $(FUNCTION_DIAGNOSTIC_TEST_OBJECT)
	@mkdir -p "$(dir $@)"
	$(CC) $^ $(MINIC_LDFLAGS) -o "$@"

$(BACKEND_MODULE_ARENA_TEST_BINARY): $(filter-out $(BUILD_DIR)/obj/tools/minic-cc/main.o,$(MINIC_OBJECTS)) $(BACKEND_MODULE_ARENA_TEST_OBJECT)
	@mkdir -p "$(dir $@)"
	$(CC) $^ $(MINIC_LDFLAGS) -o "$@"

.PHONY: check-backend-module-arena
check-backend-module-arena: $(BACKEND_MODULE_ARENA_TEST_BINARY)
	"$(BACKEND_MODULE_ARENA_TEST_BINARY)"

check-fast: check-backend-module-arena

.PHONY: check-function-diagnostic-attribute
check-function-diagnostic-attribute: $(FUNCTION_DIAGNOSTIC_TEST_BINARY)
	"$(FUNCTION_DIAGNOSTIC_TEST_BINARY)"

.PHONY: check-gnu-constant-bitfield-helper-cfg
check-gnu-constant-bitfield-helper-cfg: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		sh tests/compiler/c0/probe-gnu-constant-bitfield-helper-cfg.sh

.PHONY: check-gnu-linux-kmalloc-index-bool-guard
check-gnu-linux-kmalloc-index-bool-guard: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		sh tests/compiler/c0/probe-gnu-linux-kmalloc-index-bool-guard.sh

.PHONY: check-gnu-linux-kbuild-disjoint-bitmask-assert
check-gnu-linux-kbuild-disjoint-bitmask-assert: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
		sh tests/compiler/c0/probe-gnu-linux-kbuild-disjoint-bitmask-assert.sh

.PHONY: check-gnu-linux-memory-encode-page-boolean-range
check-gnu-linux-memory-encode-page-boolean-range: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
		sh tests/compiler/c0/probe-gnu-linux-memory-encode-page-boolean-range.sh

.PHONY: check-core-cfg-effectful-boolean-fact-barrier
check-core-cfg-effectful-boolean-fact-barrier: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		sh tests/compiler/c0/probe-core-cfg-effectful-boolean-fact-barrier.sh

check-fast: check-core-cfg-effectful-boolean-fact-barrier

check-fast: check-gnu-linux-memory-encode-page-boolean-range

.PHONY: check-gnu-linux-nvme-prp-guard
check-gnu-linux-nvme-prp-guard: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		sh tests/compiler/c0/probe-gnu-linux-nvme-prp-guard.sh

.PHONY: check-gnu-linux-i915-irq-known-zero
check-gnu-linux-i915-irq-known-zero: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		sh tests/compiler/c0/probe-gnu-linux-i915-irq-known-zero.sh

.PHONY: check-gnu-linux-bpf-union-zero
check-gnu-linux-bpf-union-zero: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		sh tests/compiler/c0/probe-gnu-linux-bpf-union-zero.sh

.PHONY: check-gnu-linux-task-mmu-swp-pfn-guard
check-gnu-linux-task-mmu-swp-pfn-guard: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		sh tests/compiler/c0/probe-gnu-linux-task-mmu-swp-pfn-guard.sh

.PHONY: check-gnu-linux-kaslr-addr-range-guard
check-gnu-linux-kaslr-addr-range-guard: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		sh tests/compiler/c0/probe-gnu-linux-kaslr-addr-range-guard.sh

.PHONY: check-gnu-linux-nf-conntrack-size
check-gnu-linux-nf-conntrack-size: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		sh tests/compiler/c0/probe-gnu-linux-nf-conntrack-size.sh

.PHONY: check-gnu-linux-ioremap-inline-arg-guard
check-gnu-linux-ioremap-inline-arg-guard: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		sh tests/compiler/c0/probe-gnu-linux-ioremap-inline-arg-guard.sh

.PHONY: check-netlink-warn-branch
check-netlink-warn-branch: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
		sh tests/compiler/c0/probe-netlink-warn-branch.sh

check-fast: check-gnu-linux-ioremap-inline-arg-guard check-netlink-warn-branch

.PHONY: check-rv32-wide-branch-condition
check-rv32-wide-branch-condition: $(MINIC_CC_BINARY)
	@MINIC_CC="$(abspath $(MINIC_CC_BINARY))" bash tests/compiler/c0/run-rv32-wide-branch-condition.sh

check-fast: check-rv32-wide-branch-condition

# RV32 integer/memory emits only sealed ILP32 CoreType; SSA values no longer
# carry a redundant source-C descriptor.
.PHONY: check-rv32-coretype-scalar
check-rv32-coretype-scalar:
	HOST_CC="$(CC)" bash tests/target/riscv32/run-coretype-scalar.sh

check-fast: check-rv32-coretype-scalar

# Both RISC-V XLENs share the same Core-only object alignment proof; the
# source-C DataLayout fallback must remain retired.
.PHONY: check-riscv-coretype-root-alignment
check-riscv-coretype-root-alignment:
	HOST_CC="$(CC)" bash tests/target/riscv/run-coretype-root-alignment.sh

check-fast: check-riscv-coretype-root-alignment

.PHONY: check-rv32-empty-record-return
check-rv32-empty-record-return: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-rv32-empty-record-return.sh

check-fast: check-rv32-empty-record-return

.PHONY: check-rv32-constructor-init-array
check-rv32-constructor-init-array: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	bash tests/compiler/c0/run-rv32-constructor-init-array.sh

check-fast: check-rv32-constructor-init-array

.PHONY: check-direct-lp64-asm-capacity
check-direct-lp64-asm-capacity: $(MINIC_CC_BINARY)
	@MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		bash tests/compiler/c0/probe-direct-lp64-asm-capacity.sh

check-fast: check-direct-lp64-asm-capacity

$(DIRECT_LP64_ASM_UNIT_BINARY): tests/compiler/c0/direct_lp64_asm_preflight_unit.c \
		src/core/core_type.c src/target/common/lp64_scalar.c \
		$(BUILD_DIR)/obj/src/frontend/type.o
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) \
		-ffunction-sections -fdata-sections "$<" \
		src/core/core_type.c src/target/common/lp64_scalar.c \
		"$(BUILD_DIR)/obj/src/frontend/type.o" $(MINIC_LDFLAGS) \
		-Wl,--gc-sections -o "$@"

.PHONY: check-direct-lp64-asm-preflight-unit
check-direct-lp64-asm-preflight-unit: $(DIRECT_LP64_ASM_UNIT_BINARY)
	"$(DIRECT_LP64_ASM_UNIT_BINARY)"

check-fast: check-direct-lp64-asm-preflight-unit

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

check-minias-a0: $(MINIAS_BINARY)
	MINIAS="$(abspath $(MINIAS_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/assembler/run-a0.sh

$(BUILD_DIR)/obj/preprocessor/%.o: preprocessor/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIPP_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD_DIR)/obj/tools/minic-cpp/%.o: tools/minic-cpp/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIPP_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(MINIPP_BINARY): $(MINIPP_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(MINIPP_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

check-minipp-a0: $(MINIPP_BINARY)
	MINIPP="$(abspath $(MINIPP_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/preprocessor/run-a0.sh

$(BUILD_DIR)/obj/archiver/%.o: archiver/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIAR_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD_DIR)/obj/tools/minic-ar/%.o: tools/minic-ar/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIAR_INCLUDES) $(MINIC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(MINIAR_BINARY): $(MINIAR_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(MINIAR_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

check-miniar-a0: $(MINIAR_BINARY)
	MINIAR="$(abspath $(MINIAR_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	HOST_CC="$(CC)" \
	sh tests/archiver/run-a0.sh

check-miniar-a1: $(MINIAR_BINARY)
	MINIAR="$(abspath $(MINIAR_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	HOST_CC="$(CC)" \
	sh tests/archiver/run-a1.sh

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

check-minielf-reader-a0:
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/elf/run-reader-a0.sh

check-mininm-a0: $(MININM_BINARY)
	MININM="$(abspath $(MININM_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/nm/run-a0.sh

check-mininm-a1: $(MININM_BINARY)
	MININM="$(abspath $(MININM_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/nm/run-a1.sh

check-miniobjcopy-a0: $(MINIOBJCOPY_BINARY)
	MINIOBJCOPY="$(abspath $(MINIOBJCOPY_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/objcopy/run-a0.sh

check-miniobjcopy-a1: $(MINIOBJCOPY_BINARY)
	MINIOBJCOPY="$(abspath $(MINIOBJCOPY_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/objcopy/run-a1.sh

check-minicstrip-a0: $(MINISTRIP_BINARY)
	MINISTRIP="$(abspath $(MINISTRIP_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/strip/run-a0.sh

check-minild-a0: $(MINILD_BINARY)
	MINILD="$(abspath $(MINILD_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/linker/run-a0.sh

check-minild-a1: $(MINILD_BINARY)
	MINILD="$(abspath $(MINILD_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/linker/run-a1.sh

check-minild-a2: $(MINILD_BINARY)
	MINILD="$(abspath $(MINILD_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/linker/run-a2.sh

check-minild-a3: $(MINILD_BINARY)
	MINILD="$(abspath $(MINILD_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/linker/run-a3.sh

check-minild-a4: $(MINILD_BINARY)
	MINILD="$(abspath $(MINILD_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/linker/run-a4.sh

check-minild-a5: $(MINILD_BINARY)
	MINILD="$(abspath $(MINILD_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/linker/run-a5.sh

check-minild-a6: $(MINILD_BINARY)
	MINILD="$(abspath $(MINILD_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/linker/run-a6.sh

check-minild-script-a0: $(MINILD_BINARY)
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/linker/run-script-a0.sh

check-minild-script-a1: $(MINILD_BINARY)
	MINILD="$(abspath $(MINILD_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/linker/run-script-a1.sh

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

$(SEMANTIC_SNAPSHOT_TEST_BINARY): $(SEMANTIC_SNAPSHOT_TEST_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(SEMANTIC_SNAPSHOT_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(TYPE_ALIAS_TEST_BINARY): $(TYPE_ALIAS_TEST_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(TYPE_ALIAS_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(AST_CONTRACT_TEST_BINARY): $(AST_CONTRACT_TEST_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(AST_CONTRACT_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(LAYOUT_TEST_BINARY): $(LAYOUT_TEST_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(LAYOUT_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

$(RV64_ABI_TEST_BINARY): $(RV64_ABI_TEST_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(RV64_ABI_TEST_OBJECTS) $(MINIC_LDFLAGS) -o "$@"

check-token-model: $(TOKEN_MODEL_TEST_BINARY)
	"$(abspath $(TOKEN_MODEL_TEST_BINARY))"

check-lexer: $(LEXER_TEST_BINARY)
	"$(abspath $(LEXER_TEST_BINARY))"

.PHONY: check-binary128-literal-oracle
check-binary128-literal-oracle:
	@mkdir -p "$(BUILD_DIR)/tests/frontend"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) \
		src/frontend/binary128_literal.c tests/frontend/binary128_literal_oracle_test.c \
		$(MINIC_LDFLAGS) -o "$(BUILD_DIR)/tests/frontend/binary128_literal_oracle_test"
	"$(abspath $(BUILD_DIR))/tests/frontend/binary128_literal_oracle_test"

.PHONY: check-core-language-boundary
check-core-language-boundary:
	bash tests/core/check-core-language-boundary.sh "$(CC)"

.PHONY: check-target-abstraction-boundary
check-target-abstraction-boundary:
	bash tests/core/check-target-abstraction-boundary.sh "$(CC)"

# check-core-types already executes the Core language boundary script.
check-fast: check-target-abstraction-boundary

.PHONY: check-core-types
check-core-types:
	bash tests/core/run-core-types.sh "$(CC)"

.PHONY: check-core-ir-shapes
check-core-ir-shapes: check-core-types
	bash tests/core/run-core-ir-shapes.sh "$(CC)"

.PHONY: check-core-ir-shapes-source
check-core-ir-shapes-source: $(MINIC_CC_BINARY)
	bash tests/core/run-core-ir-shapes-source.sh "$(abspath $(MINIC_CC_BINARY))"

.PHONY: check-core-type-verifier
check-core-type-verifier:
	bash tests/core/run-core-type-verifier.sh "$(CC)"

# Keep the CoreType vector extent/ABI contract independently testable: no
# target ISA, frontend snapshot, cross compiler or Linux image is required.
.PHONY: check-core-vector-layout-contract
check-core-vector-layout-contract:
	@mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  src/core/core_type.c \
	  tests/compiler/c0/probe-core-vector-layout-contract.c \
	  -o "$(BUILD_DIR)/tests/core-vector-layout-contract"
	"$(BUILD_DIR)/tests/core-vector-layout-contract"

check-fast: check-core-vector-layout-contract

# Source-free seal for complete record field bounds, bitfields and flexible
# array tails, independently of any target ABI or cross-toolchain.
.PHONY: check-core-record-layout-contract
check-core-record-layout-contract:
	@mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  src/core/core_type.c \
	  tests/compiler/c0/probe-core-record-layout-contract.c \
	  -o "$(BUILD_DIR)/tests/core-record-layout-contract"
	"$(BUILD_DIR)/tests/core-record-layout-contract"

# No physical memory action for empty GNU records. GNU and MiniC must both
# execute the pointer-producing expressions, on a real native ELF.
.PHONY: check-zero-record-five-targets
check-zero-record-five-targets: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/probe-zero-record-five-targets.sh

check-fast: check-core-record-layout-contract check-zero-record-five-targets

RV64_CORETYPE_TARGET_ADMISSION_BINARY := $(BUILD_DIR)/tests/target/riscv64/coretype-target-admission
$(RV64_CORETYPE_TARGET_ADMISSION_BINARY): tests/target/riscv64/coretype_target_admission_test.c $(MINIC_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) \
		tests/target/riscv64/coretype_target_admission_test.c \
		$(filter-out $(BUILD_DIR)/obj/tools/minic-cc/main.o,$(MINIC_OBJECTS)) \
		$(MINIC_LDFLAGS) -o "$@"

.PHONY: check-rv64-coretype-target-admission
check-rv64-coretype-target-admission: $(RV64_CORETYPE_TARGET_ADMISSION_BINARY)
	sh tests/target/riscv64/probe-coretype-target-admission.sh "$(abspath $(RV64_CORETYPE_TARGET_ADMISSION_BINARY))"

check-type: $(TYPE_TEST_BINARY)
	"$(abspath $(TYPE_TEST_BINARY))"

check-record: $(RECORD_TEST_BINARY)
	"$(abspath $(RECORD_TEST_BINARY))"

check-semantic-snapshot: $(SEMANTIC_SNAPSHOT_TEST_BINARY)
	"$(abspath $(SEMANTIC_SNAPSHOT_TEST_BINARY))"

check-type-alias: $(TYPE_ALIAS_TEST_BINARY)
	"$(abspath $(TYPE_ALIAS_TEST_BINARY))"

check-ast-contract: $(AST_CONTRACT_TEST_BINARY)
	"$(abspath $(AST_CONTRACT_TEST_BINARY))"

check-layout: $(LAYOUT_TEST_BINARY)
	"$(abspath $(LAYOUT_TEST_BINARY))"

check-rv64-abi: $(RV64_ABI_TEST_BINARY)
	"$(abspath $(RV64_ABI_TEST_BINARY))"

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

# LP64 type-model ABI oracle for the two registered (not yet emitted) ISAs.
# Link to the same *already compiled* MiniC library objects as minic-cc.
# Shared GNU/ELF/QEMU runner, independently selected architecture/ABI tools.
# Run only with both cross-GCC toolchains and qemu-user actually installed.
# M2 uses the same proven cross-GNU/ELF/QEMU harness with a larger Core
# CFG corpus; M1 remains an independent early-ISA smoke gate.
check-four-backend-m2: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M2=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# Keep the baseline M1/M2 suites independent; GNU -> MiniC 9/10-argument
# tests live in an opt-in extension of the proven ELF/QEMU harness.
check-four-backend-m2-stack: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M2_STACK=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# GNU cross-TU and actual MiniC nested callers; the next observed x86
# sequence gate before attempting unchanged tinyAES/cJSON source.
check-four-backend-m3-call: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M3_CALL=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# Narrow static integer and byte array globals, and explicit relocation rejection.
check-four-backend-m4-global: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M4_GLOBAL=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# Original AES_init_ctx first signature frontier: pointer and void ABI.
check-four-backend-m5-pointer: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M5_POINTER=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# Source-observed first original AES_init_ctx field.addr / pointer.offset:
# GNU->MiniC on real struct and array storage, both target QEMUs.
check-four-backend-m6-address: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M6_ADDRESS=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# Local object layout and >2KiB stack displacements validated by
# independent cross-GNU static link and genuine target QEMU execution.
check-four-backend-m7-frame: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M7_FRAME=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# Original KeyExpansion uint8_t and integer-conversion Core frontier:
# real C signed/unsigned 1B/2B memory and conversions, GNU-linked QEMU.
check-four-backend-m8-narrow: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M8_NARROW=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# Actual AES KeyExpansion signedness frontier: unsigned 32-bit compare
# and offset, independently built by GNU and through target QEMU.
check-four-backend-m9-unsigned: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M9_UNSIGNED=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# GNU/QEMU-backed signed/unsigned divide and remainder ABI.
check-four-backend-m10-mod: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M10_MOD=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# Actual original AES pointer.offset with narrow signed/unsigned index:
# distinct GNU and MiniC ELF + QEMU cross execution.
check-four-backend-m11-index: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M11_INDEX=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# Original AES KeyExpansion AND/XOR/OR target-owned machine lowering.
check-four-backend-m12-bitwise: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M12_BITWISE=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# TinyAES AddRoundKey actual uint8_t add/sub/mul: both cross-GNU/QEMU
# with independent oracle executables to check width and signedness.
check-four-backend-m13-narrow-arith: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M13_NARROW_ARITH=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# True target variable signed/unsigned shifts in original xtime:
# independent GNU and MiniC static ELF/QEMU executables per ISA.
check-four-backend-m14-shift: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M14_SHIFT=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# xtime authentic uint8_t result ABI, including signed char and
# native-vs-GNU callee return normalization under both target QEMUs.
check-four-backend-m15-scalar-return: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M15_SCALAR_RETURN=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# First pinned, unchanged real project in the same x86 promotion order:
# a blocked compiler frontier is printed honestly, not a runtime PASS.
check-four-backend-tinyaes-frontier: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	sh tests/external/tiny-aes-c/probe-four-backend-frontier.sh

# Strict production acceptance: BOTH unmodified original AES-128 ECB
# binaries must be MiniC compiled, GNU linked, and QEMU-byte-exact.
check-four-backend-tinyaes-accept: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_TINYAES_REQUIRE=1 \
	sh tests/external/tiny-aes-c/probe-four-backend-frontier.sh

# Historical x86 order: only after original tinyAES 2/2 passes, promote
# the exact already-pinned cJSON 1.7.19 core and its existing test gates.
# GNU may preprocess/assemble/link, never compile the accepted core.
check-four-backend-cjson-frontier: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	sh tests/external/cjson/probe-four-backend-frontier.sh

check-four-backend-cjson-accept: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_CJSON_REQUIRE=1 \
	sh tests/external/cjson/probe-four-backend-frontier.sh

# Next original real project after tinyAES and cJSON: all 15 pinned zlib
# library TUs, GNU cross-oracle/QEMU, error-census discovery before acceptance.
check-four-backend-zlib-frontier: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	sh tests/external/zlib/probe-four-backend-frontier.sh

check-four-backend-zlib-accept: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_ZLIB_REQUIRE=1 \
	sh tests/external/zlib/probe-four-backend-frontier.sh

# Lua 5.5.0 official 34-TU original source and cross-GNU/QEMU oracle.
# Frontier remains available for diagnosis; CI uses the strict acceptance.
check-four-backend-lua-frontier: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	sh tests/external/lua/probe-four-backend-frontier.sh

check-four-backend-lua-accept: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_LUA_REQUIRE=1 \
	sh tests/external/lua/probe-four-backend-frontier.sh

# Original SQLite 3.54.1 two-ISA amalgamation. Frontier is discovery only.
# A status=BLOCKED never counts as project acceptance.
check-four-backend-sqlite-frontier: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	sh tests/external/sqlite/probe-four-backend-frontier.sh

check-four-backend-sqlite-accept: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_SQLITE_REQUIRE=1 \
	sh tests/external/sqlite/probe-four-backend-frontier.sh

# Real cJSON M17 flat record and ELF callback relocations; independent
# GNU caller checks target DataLayout field bytes under both QEMUs.
check-four-backend-m17-record: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M17_RECORD=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# cJSON_Version's original sprintf uses only promoted integer and pointer
# variadic parameters, verified as GNU ABI cross-TU ELF/QEMU operations.
check-four-backend-m18-varargs: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M18_VARARGS=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# cJSON_InitHooks: true 64-bit data/function pointer comparison, null
# predicates and actual ELF first-class function symbol addresses.
check-four-backend-m19-ptr-compare: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" MINIC_M1_TRACE=1 FOUR_BACKEND_M19_PTR_COMPARE=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# cJSON initializes size_t and handles signed/unsigned scalar casts.
# Full-width cJSON size_t equality, ordering, sign and zero predicates.
# cJSON global_hooks: native first-class function pointer calls, GNU/QEMU.
# Source-driven cJSON_GetNumberValue: binary64 payload and FP-return ABI.
# cJSON_GetNumberValue's NaN literal arises from IEEE 0/0 division.
# Original cJSON_CreateNumber: independently classified LP64d floating params.
# cJSON_CreateNumber's signed32/IEEE binary64 numeric conversion frontier.
# cJSON_CreateNumber: NaN-correct IEEE-754 ordered double comparisons.
# cJSON_CreateIntArray forwards binary64 to a fixed-parameter callee.
# cJSON_CreateFloatArray: IEEE float32 memory and numeric binary64 promotion.
# Original cJSON_Compare explicit Core unreachable terminator, checked with GNU/QEMU.
# cJSON_SetValuestring ordered 64-bit pointer overlap checks.
# cJSON parse_number's GNU strtod and callback binary64 result ABI.
# Original cJSON parse_number: LP64 pointer-to-integer and byte offsets.
# Original cJSON ensure(): size_t multiplication with full LP64 semantics.
# Original cJSON compare_double: IEEE binary64 subtract and multiply.
# Original cJSON print_number: ABI-specific variadic binary64 calling.
# M39: original cJSON utf16_literal_to_utf8 uses unsigned long
# bitwise masks and shifts. Assert LP64 width with GNU/QEMU on both ISAs.
check-four-backend-m39-lp64-bitops: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M39_LP64_BITOPS=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m38-fp-varargs: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M38_FP_VARARGS=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m37-fp-arith: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M37_FP_ARITH=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m36-lp64-mul: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M36_LP64_MUL=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m35-ptr-to-lp64: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M35_PTR_TO_LP64=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m34-fp-call-result: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M34_FP_CALL_RESULT=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m33-pointer-less: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M33_POINTER_LESS=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m32-unreachable: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M32_UNREACHABLE=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m31-float-promote: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M31_FLOAT_PROMOTE=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m30-fp-call: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M30_FP_CALL=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m29-double-cmp: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M29_DOUBLE_CMP=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m28-int-double: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M28_INT_DOUBLE=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m27-fp-parameter: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M27_FP_PARAMETER=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m26-double-div: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M26_DOUBLE_DIV=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m25-double-transport: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M25_DOUBLE_TRANSPORT=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m24-indirect-call: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M24_INDIRECT_CALL=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m23-lp64-compare: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M23_LP64_COMPARE=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m22-lp64-conv: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M22_LP64_CONV=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# cJSON's strlen(value)+sizeof("") is native size_t arithmetic.
check-four-backend-m21-lp64-add: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M21_LP64_ADD=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

# cJSON's size_t frontier: ABI high-half, scalar memory and GNU callers.
check-four-backend-m20-lp64-abi: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" FOUR_BACKEND_M20_LP64_ABI=1 \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-backend-m1: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	sh tests/compiler/c0/run-four-backend-m1.sh

check-four-target-model-m0: $(MINIC_CC_BINARY)
	@mkdir -p "$(BUILD_DIR)/tests/target"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) \
		tests/compiler/c0/check-four-target-model-m0.c \
		$(filter-out $(BUILD_DIR)/obj/tools/minic-cc/main.o,$(MINIC_OBJECTS)) \
		$(MINIC_LDFLAGS) -o "$(BUILD_DIR)/tests/target/four-target-model-m0"
	"$(BUILD_DIR)/tests/target/four-target-model-m0"

check-target-selection-m0: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	sh tests/compiler/c0/run-target-selection-m0.sh

# Target-owned SysV struct/in_addr and transparent sockaddr pointer-union
# regression. Keep the existing M1 and cross-target checks separate.
# GNU ELF startup correctness, not a compilation-only attribute test.
check-elf-constructors: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	HOST_CC="$(CC)" sh tests/compiler/c0/run-elf-constructors.sh

check-x86_64-one-gp-sysv: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	HOST_CC="$(CC)" \
	sh tests/compiler/c0/run-x86_64-one-gp-sysv.sh

.PHONY: check-x86_64-sysv-odd-record-abi
check-x86_64-sysv-odd-record-abi: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-x86-sysv-odd-record-abi.sh

check-fast: check-x86_64-sysv-odd-record-abi

check-x86_64-m1: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	HOST_CC="$(CC)" \
	sh tests/compiler/c0/run-x86_64-native-m1.sh

check-x86_64-tinyaes: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	HOST_CC="$(CC)" \
	sh tests/external/tiny-aes-c/probe-x86_64.sh

check-x86_64-cjson: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	HOST_CC="$(CC)" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/external/cjson/probe-x86_64-differential.sh

check-x86_64-abi-elf: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	HOST_CC="$(CC)" \
	sh tests/compiler/c0/run-x86_64-abi-layout-elf.sh

check-x86_64-lua-frontier: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	HOST_CC="$(CC)" \
	sh tests/external/lua/probe-x86_64.sh

check-x86_64-zlib-frontier: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	HOST_CC="$(CC)" \
	sh tests/external/zlib/probe-x86_64.sh

# New real-project pressure: official pristine SQLite amalgamation.
# Set SQLITE_X86_64_DISCOVERY=1 to classify a blocker without claiming a pass.
check-x86_64-sqlite-frontier: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	HOST_CC="$(CC)" \
	sh tests/external/sqlite/probe-x86_64.sh

# Durable 12-object original BusyBox Kbuild acceptance. The underlying probe
# keeps extra cases as opt-in discovery; this target requires its precise final
# byte-for-byte native execution marker, so a discovery-mode early exit is RED.
# GNU remains the other-object producer, assembler, linker and independent oracle.
# Clear outer MiniC warning flags and make command-line overrides before
# invoking pristine upstream BusyBox Kbuild inside the reusable probe.
check-x86_64-busybox-12: $(MINIC_CC_BINARY)
	@set -eu; \
	log=$$(mktemp); \
	trap 'rm -f "$$log"' EXIT HUP INT TERM; \
	if env -u MAKEFLAGS -u MAKEOVERRIDES -u MFLAGS -u GNUMAKEFLAGS -u CFLAGS \
	   MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	   BUSYBOX_X86_64_EXTRA_DISCOVERY=1 \
	   sh tests/external/busybox/probe-x86_64.sh >"$$log" 2>&1; then \
	   cat "$$log"; \
	else \
	   cat "$$log"; exit 1; \
	fi; \
	grep -Fx 'BUSYBOX_X86_64_EXTRA_MIXED=PASS applets=basename,dirname,printf extra_objects=3 total_MiniC_objects=12 GCC_reference=byte-exact GNU_Kbuild=1' "$$log"

# Discovery, not certification: original upstream allyesconfig + real Kbuild
# CC interception. GNU still builds every Kbuild object. The MiniC shadow path
# records all attempted original C owners, including hard failures.
# Separate from the default 963-symbol discovery scan: keep all supported
# software enabled, disable only six host-incompatible configuration switches
# and require a real original-GNU BusyBox link + six actual applet executions.
# MiniC shadows Kbuild C owners but does NOT claim to link them in this gate.
# Real upstream 957-option GNU BusyBox baseline, then five independently
# substituted original C object owners (sed, awk, find, grep, sort) and all
# eleven differential runtime cases per cohort. Unlike compile-only scanning
# the MiniC .o is incorporated in actual Kbuild linkage. Fail closed.
# Original 957-symbol Kconfig/Kbuild. Replace EVERY independent verified
# MiniC x86 target owner (currently 582/594), retain explicit GNU fallbacks.
# Verify every original owner receipt+SHA and compare whole linked executable
# against original GNU across 11 deterministic applet cases. Fail closed.
check-x86_64-busybox-all-eligible: $(MINIC_CC_BINARY)
	@python3 tests/external/busybox/test_mixed_bulk_coverage.py
	@set -eu; \
	log=$$(mktemp); \
	trap 'rm -f "$$log"' EXIT HUP INT TERM; \
	if env -u MAKEFLAGS -u MAKEOVERRIDES -u MFLAGS -u GNUMAKEFLAGS -u CFLAGS \
	   MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	   BUSYBOX_X86_64_ALLYES_DISCOVERY=1 BUSYBOX_ALLYES_LINK_COMPATIBLE=1 \
	   BUSYBOX_ALLYES_MIXED_BULK=1 \
	   BUSYBOX_ALLYES_REPORT_DIR="$(abspath $(BUILD_DIR))/busybox-bulk-mixed" \
	   sh tests/external/busybox/probe-x86_64.sh >"$$log" 2>&1; then \
	   cat "$$log"; \
	else \
	   cat "$$log"; exit 1; \
	fi; \
	grep -Fx 'BUSYBOX_MIXED_BULK=PASS MiniC_owners=593 GCC_fallback=0 original_full_Kbuild=1 eleven_fixture_byte_exact=1 MiniC_and_GNU_binary_differ=1 full_all_MiniC=NO' "$$log"; \
	grep -Fx 'BUSYBOX_MIXED_BULK_ALL_C_OWNERS=PASS original_Kbuild_owners=593 MiniC_owners=593 GNU_C_fallbacks=0 SHA256_verified=593 link_toolchain=GNU' "$$log"; \
	grep -Fx 'BUSYBOX_ALLYES_FRONTIER=DISCOVERY enabled_symbols=957 attempted_C_TUs=594 mini_objects=594 cpp_fail=0 minic_fail=0 asm_fail=0 gcc_fail=0 gcc_full_build=PASS full_MiniC_busybox=NOT_TESTED' "$$log"

check-x86_64-busybox-complex-five: $(MINIC_CC_BINARY)
	@set -eu; \
	log=$$(mktemp); \
	trap 'rm -f "$$log"' EXIT HUP INT TERM; \
	if env -u MAKEFLAGS -u MAKEOVERRIDES -u MFLAGS -u GNUMAKEFLAGS -u CFLAGS \
	   MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	   BUSYBOX_X86_64_ALLYES_DISCOVERY=1 BUSYBOX_ALLYES_LINK_COMPATIBLE=1 \
	   BUSYBOX_ALLYES_MIXED_COHORT=1 \
	   BUSYBOX_ALLYES_REPORT_DIR="$(abspath $(BUILD_DIR))/busybox-complex-five" \
	   sh tests/external/busybox/probe-x86_64.sh >"$$log" 2>&1; then \
	   cat "$$log"; \
	else \
	   cat "$$log"; exit 1; \
	fi; \
	grep -Fx 'BUSYBOX_MIXED_COMPLEX=PASS all_five_independent=1 combined_five=1 GCC_original_oracle=1 real_Kbuild_link=1' "$$log"

check-x86_64-busybox-host-link: $(MINIC_CC_BINARY)
	@set -eu; \
	log=$$(mktemp); \
	trap 'rm -f "$$log"' EXIT HUP INT TERM; \
	if env -u MAKEFLAGS -u MAKEOVERRIDES -u MFLAGS -u GNUMAKEFLAGS -u CFLAGS \
	   MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	   BUSYBOX_X86_64_ALLYES_DISCOVERY=1 BUSYBOX_ALLYES_LINK_COMPATIBLE=1 \
	   BUSYBOX_ALLYES_REPORT_DIR="$(abspath $(BUILD_DIR))/busybox-host-link" \
	   sh tests/external/busybox/probe-x86_64.sh >"$$log" 2>&1; then \
	   cat "$$log"; \
	else \
	   cat "$$log"; exit 1; \
	fi; \
	grep -Fx 'BUSYBOX_ALLYES_GCC_RUNTIME_ORACLE=PASS applets=6 original_full_Kbuild=1' "$$log"

check-x86_64-busybox-allyes-discovery: $(MINIC_CC_BINARY)
	@set -eu; \
	env -u MAKEFLAGS -u MAKEOVERRIDES -u MFLAGS -u GNUMAKEFLAGS -u CFLAGS \
	  MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	  BUSYBOX_X86_64_ALLYES_DISCOVERY=1 \
	  BUSYBOX_ALLYES_REPORT_DIR="$(abspath $(BUILD_DIR))/busybox-allyes-sweep" \
	  sh tests/external/busybox/probe-x86_64.sh

.PHONY: check-x86-linux-control-state
check-x86-linux-control-state: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/target/run-linux-x86-control-state.sh

.PHONY: check-linux-three-arch-trap-state
check-linux-three-arch-trap-state: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/target/run-linux-three-arch-trap-state.sh

.PHONY: check-x86-linux-uaccess-and-abi
check-x86-linux-uaccess-and-abi: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/target/run-linux-x86-uaccess-and-abi.sh

.PHONY: check-linux-symbolic-integer-and-record
check-linux-symbolic-integer-and-record: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/target/run-linux-symbolic-integer-and-record.sh

.PHONY: check-packed-bitfield-field-layout
check-packed-bitfield-field-layout: $(MINIC_CC_BINARY)
	bash tests/compiler/c0/run-packed-bitfield-field-layout.sh "$(abspath $(MINIC_CC_BINARY))"

.PHONY: check-packed-zero-width-bitfield-barrier
check-packed-zero-width-bitfield-barrier: $(MINIC_CC_BINARY)
	bash tests/compiler/c0/run-packed-zero-width-bitfield-barrier.sh "$(abspath $(MINIC_CC_BINARY))"

.PHONY: check-long-double-literal-precision
check-long-double-literal-precision: $(MINIC_CC_BINARY)
	bash tests/compiler/c0/run-long-double-literal-precision.sh "$(abspath $(MINIC_CC_BINARY))"

.PHONY: check-asm-operand-plan
check-asm-operand-plan: $(MINIC_CC_BINARY)
	bash tests/core/run-asm-operand-plan.sh "$(abspath $(MINIC_CC_BINARY))"

.PHONY: check-loongarch64-record-copy-size
check-loongarch64-record-copy-size: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	bash tests/compiler/c0/probe-loongarch64-record-copy-size.sh

.PHONY: check-loongarch64-frame-fold
check-loongarch64-frame-fold: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-loongarch64-frame-fold.sh

.PHONY: check-core-value-use-counts
check-core-value-use-counts:
	bash tests/compiler/c0/probe-core-value-use-counts.sh

.PHONY: check-core-call-dump
check-core-call-dump:
	HOST_CC="$(CC)" bash tests/compiler/c0/probe-core-call-dump.sh

.PHONY: check-mc089-x87-ordered-comparisons
check-mc089-x87-ordered-comparisons: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-mc089-x87-ordered-comparisons.sh

check-fast: check-mc089-x87-ordered-comparisons

check-fast: check-core-call-dump

.PHONY: check-core-dead-ssa-writeback
check-core-dead-ssa-writeback: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-core-dead-ssa-writeback.sh

.PHONY: check-core-gp-forward
check-core-gp-forward: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-core-gp-forward.sh

.PHONY: check-core-literal-branch-guard
check-core-literal-branch-guard: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	bash tests/compiler/c0/probe-core-literal-branch-guard.sh

.PHONY: check-core-literal-branch-fold
check-core-literal-branch-fold:
	bash tests/compiler/c0/probe-core-literal-branch-fold.sh

.PHONY: check-core-function-designator-nonnull
check-core-function-designator-nonnull: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	bash tests/compiler/c0/probe-core-function-designator-nonnull.sh

check-fast: check-loongarch64-record-copy-size check-loongarch64-frame-fold check-core-value-use-counts check-core-dead-ssa-writeback check-core-gp-forward check-core-literal-branch-guard check-core-literal-branch-fold check-core-function-designator-nonnull

.PHONY: check-gnu-builtin-constant-p-if-guard
check-gnu-builtin-constant-p-if-guard: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/run-gnu-builtin-constant-p-if-guard.sh

.PHONY: check-gnu-runtime-if-effect-facts
check-gnu-runtime-if-effect-facts: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/run-gnu-runtime-if-effect-facts.sh

.PHONY: check-gnu-if-condition-local-owner
check-gnu-if-condition-local-owner: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/run-gnu-if-condition-local-owner.sh

.PHONY: check-elf-hidden-undefined-pic-source
check-elf-hidden-undefined-pic-source: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" MINIC_SOURCE_ONLY=1 \
	sh tests/compiler/c0/probe-elf-hidden-undefined-pic.sh

.PHONY: check-x86-pic-hidden-undefined-weak
check-x86-pic-hidden-undefined-weak: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-x86-pic-hidden-undefined-weak.sh

check-fast: check-x86-pic-hidden-undefined-weak

.PHONY: check-x86-vdso-hidden-unused
check-x86-vdso-hidden-unused: $(MINIC_CC_BINARY)
	HOST_CC="$(CC)" sh tests/compiler/c0/probe-x86-vdso-hidden-unused.sh \
		"$(abspath $(MINIC_CC_BINARY))"

.PHONY: check-x86-reachable-gnu-runtime
check-x86-reachable-gnu-runtime: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-x86-reachable-gnu-runtime.sh

.PHONY: check-x86-kernel-head-link-address
check-x86-kernel-head-link-address: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-x86-kernel-head-link-address.sh

check-fast: check-x86-kernel-head-link-address

.PHONY: check-core-indirect-local-fact-escape
check-core-indirect-local-fact-escape: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-core-indirect-local-fact-escape.sh

check-fast: check-core-indirect-local-fact-escape

.PHONY: check-external-char-array-redeclaration
check-external-char-array-redeclaration: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-external-char-array-redeclaration.sh

check-fast: check-external-char-array-redeclaration

.PHONY: check-tagged-bare-record-members
check-tagged-bare-record-members: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-tagged-bare-record-members.sh

check-fast: check-tagged-bare-record-members

.PHONY: check-anonymous-qualified-record-members
check-anonymous-qualified-record-members: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-anonymous-qualified-record-members.sh

check-fast: check-anonymous-qualified-record-members

.PHONY: check-incomplete-enum-coretype
check-incomplete-enum-coretype: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" bash tests/compiler/c0/probe-incomplete-enum-coretype.sh

check-fast: check-incomplete-enum-coretype

.PHONY: check-core-late-live-closure
check-core-late-live-closure: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		bash tests/compiler/c0/probe-core-late-live-closure.sh

check-fast: check-core-late-live-closure

.PHONY: check-x86-linux-smp-xadd-i64
check-x86-linux-smp-xadd-i64: $(MINIC_CC_BINARY)
	bash tests/compiler/c0/probe-x86-linux-abi-carry-xadd.sh "$(abspath $(MINIC_CC_BINARY))"

check-fast: check-x86-linux-smp-xadd-i64

.PHONY: check-x86-linux-percpu-xadd-enum
check-x86-linux-percpu-xadd-enum: $(MINIC_CC_BINARY)
	bash tests/compiler/c0/probe-x86-linux-percpu-xadd-enum.sh "$(abspath $(MINIC_CC_BINARY))"

check-fast: check-x86-linux-percpu-xadd-enum

.PHONY: check-record-field-declarator-realloc
check-record-field-declarator-realloc: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-record-field-declarator-realloc.sh

check-fast: check-record-field-declarator-realloc

.PHONY: check-record-field-function-pointer-realloc
check-record-field-function-pointer-realloc: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-record-field-function-pointer-realloc.sh

check-fast: check-record-field-function-pointer-realloc

.PHONY: check-static-chained-array-arena
check-static-chained-array-arena: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-static-chained-array-arena.sh

check-fast: check-static-chained-array-arena

.PHONY: check-rv32-pointer-widening-source check-rv32-pointer-widening-qemu
check-rv32-pointer-widening-source: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-rv32-pointer-widening-source.sh

check-rv32-pointer-widening-qemu: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	bash tests/compiler/c0/probe-rv32-pointer-widening-qemu.sh

check-fast: check-rv32-pointer-widening-source

.PHONY: check-parameter-tag-scope
check-parameter-tag-scope: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-parameter-tag-scope.sh

check-fast: check-parameter-tag-scope

.PHONY: check-owned-call-designator-rollback
check-owned-call-designator-rollback: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-owned-call-designator-rollback.sh

check-fast: check-owned-call-designator-rollback

.PHONY: check-static-record-backward-arena
check-static-record-backward-arena: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-static-record-backward-arena.sh

check-fast: check-static-record-backward-arena

.PHONY: check-x86-head-inline-section
check-x86-head-inline-section: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-x86-head-inline-section.sh

check-fast: check-x86-head-inline-section

.PHONY: check-attributed-record-forward-declarations
check-attributed-record-forward-declarations: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-attributed-record-forward-declarations.sh

check-fast: check-attributed-record-forward-declarations

.PHONY: check-runtime-nested-array-arena-growth
check-runtime-nested-array-arena-growth: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-runtime-nested-array-arena-growth.sh

.PHONY: check-runtime-record-positional-arena-growth
check-runtime-record-positional-arena-growth: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-runtime-record-positional-arena-growth.sh

check-fast: check-runtime-nested-array-arena-growth check-runtime-record-positional-arena-growth

.PHONY: check-mc035-runtime-range-once-arena
check-mc035-runtime-range-once-arena: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-mc035-runtime-range-once-arena.sh

check-fast: check-mc035-runtime-range-once-arena

.PHONY: check-runtime-record-tagged-arena-growth
check-runtime-record-tagged-arena-growth: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-runtime-record-tagged-arena-growth.sh

check-fast: check-runtime-record-tagged-arena-growth

.PHONY: check-return-assignment-expression
check-return-assignment-expression: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-return-assignment-expression.sh

check-fast: check-return-assignment-expression

.PHONY: check-builtin-lexeme-contract
check-builtin-lexeme-contract: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-builtin-lexeme-contract.sh

check-fast: check-builtin-lexeme-contract

.PHONY: check-mc001-symbolic-closure-arena
check-mc001-symbolic-closure-arena: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-mc001-symbolic-closure-arena.sh

check-fast: check-mc001-symbolic-closure-arena

.PHONY: check-mc006-shared-call-relocations
check-mc006-shared-call-relocations: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-mc006-shared-call-relocations.sh

check-fast: check-mc006-shared-call-relocations

.PHONY: check-mc033-statement-update-facts
check-mc033-statement-update-facts: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-mc033-statement-update-facts.sh

check-fast: check-mc033-statement-update-facts

.PHONY: check-mc036-record-conditional-facts
check-mc036-record-conditional-facts: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-mc036-record-conditional-facts.sh

check-fast: check-mc036-record-conditional-facts

.PHONY: check-mc038-anonymous-designator-runtime
check-mc038-anonymous-designator-runtime: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-mc038-anonymous-designator-runtime.sh

check-fast: check-mc038-anonymous-designator-runtime

.PHONY: check-alignment-attribute-semantic-arena
check-alignment-attribute-semantic-arena: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-alignment-attribute-semantic-arena.sh

check-fast: check-alignment-attribute-semantic-arena

.PHONY: check-mc034-static-designator-pointer-pair
check-mc034-static-designator-pointer-pair: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-mc034-static-designator-pointer-pair.sh

check-fast: check-mc034-static-designator-pointer-pair

.PHONY: check-x86-pat-clflushopt-alt
check-x86-pat-clflushopt-alt: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-x86-pat-clflushopt-alt.sh

check-fast: check-x86-pat-clflushopt-alt

.PHONY: check-static-local-owner-reachability
check-static-local-owner-reachability: $(MINIC_CC_BINARY)
	MINIC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	bash tests/compiler/c0/run-static-local-owner-reachability.sh

.PHONY: check-direct-lp64-reachable-source-labels
check-direct-lp64-reachable-source-labels: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/probe-direct-lp64-reachable-source-labels.sh

.PHONY: check-rv64-pic-elf-nonpreemptible
check-rv64-pic-elf-nonpreemptible: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" MINIC_PIC_ASM_ONLY=1 \
	sh tests/compiler/c0/probe-rv64-pic-glibc-got.sh

.PHONY: check-aarch64-fixmap-cross-static-const
check-aarch64-fixmap-cross-static-const: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-aarch64-fixmap-cross-static-const.sh

.PHONY: check-aarch64-fp-pointer-record-abi
check-aarch64-fp-pointer-record-abi: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-aarch64-fp-pointer-record-abi.sh

check-fast: check-aarch64-fp-pointer-record-abi

.PHONY: check-core-overflow-enum-origin
check-core-overflow-enum-origin: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	bash tests/compiler/c0/probe-core-overflow-enum-origin.sh

check-fast: check-core-overflow-enum-origin

.PHONY: check-recursive-zero-int128-global-source
check-recursive-zero-int128-global-source: $(MINIC_CC_BINARY)
	MINIC_RECURSIVE_INT128_SOURCE_ONLY=1 \
	bash tests/compiler/c0/probe-recursive-zero-int128-global.sh \
	"$(abspath $(MINIC_CC_BINARY))"

check-fast: check-recursive-zero-int128-global-source

.PHONY: check-core-module-rollback
check-core-module-rollback:
	@mkdir -p "$(BUILD_DIR)/tests/core"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) \
		tests/core/module_ir_rollback_test.c \
		src/core/module_ir.c src/core/core_type.c $(MINIC_LDFLAGS) \
		-o "$(BUILD_DIR)/tests/core/module-ir-rollback"
	"$(abspath $(BUILD_DIR))/tests/core/module-ir-rollback"

check-fast: check-core-module-rollback

.PHONY: check-core-module-fragment-contract
check-core-module-fragment-contract:
	@mkdir -p "$(BUILD_DIR)/tests/core"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) \
		tests/core/module_ir_fragment_contract_test.c \
		src/core/module_ir.c src/core/core_type.c $(MINIC_LDFLAGS) \
		-o "$(BUILD_DIR)/tests/core/module-ir-fragment-contract"
	"$(abspath $(BUILD_DIR))/tests/core/module-ir-fragment-contract"

check-fast: check-core-module-fragment-contract

.PHONY: check-inline-specialize-common
check-inline-specialize-common:
	@mkdir -p "$(BUILD_DIR)/tests/compiler"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) \
		tests/compiler/inline_specialize_common_test.c \
		src/compiler/inline_specialize_common.c $(MINIC_LDFLAGS) \
		-o "$(BUILD_DIR)/tests/compiler/inline-specialize-common"
	"$(abspath $(BUILD_DIR))/tests/compiler/inline-specialize-common"

check-fast: check-inline-specialize-common

.PHONY: check-p43-direct-record-callsite
check-p43-direct-record-callsite: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		bash tests/compiler/c0/probe-p43-direct-record-callsite.sh

check-fast: check-p43-direct-record-callsite

.PHONY: check-p43-inline-call-plan-identity
check-p43-inline-call-plan-identity:
	HOST_CC="$(CC)" bash tests/compiler/c0/probe-p43-inline-call-plan-identity.sh

check-fast: check-p43-inline-call-plan-identity

.PHONY: check-p44-live-asm-roots
check-p44-live-asm-roots: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		bash tests/compiler/c0/probe-p44-live-asm-roots.sh

check-fast: check-p44-live-asm-roots

.PHONY: check-p44-mutating-symbolic-asm
check-p44-mutating-symbolic-asm: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		bash tests/compiler/c0/probe-p44-inline-asm-mutating-symbolic.sh

check-fast: check-p44-mutating-symbolic-asm

.PHONY: check-mc058-specialization-correctness
check-mc058-specialization-correctness: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
		HOST_CC="$(CC)" \
		bash tests/compiler/c0/probe-mc058-specialization-correctness.sh

check-fast: check-mc058-specialization-correctness

# P41: verify the C type/fact boundary as a single GNU differential family.
.PHONY: check-p41-typed-asm-and-live-facts
check-p41-typed-asm-and-live-facts: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		bash tests/compiler/c0/probe-p41-asm-specialized-integer-width.sh
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		bash tests/compiler/c0/probe-p41-bounded-or-specialization.sh
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
		bash tests/compiler/c0/probe-p41-volatile-record-member.sh

check-fast: check-p41-typed-asm-and-live-facts

.PHONY: check-loongarch64-linux-va-fixed-overflow
check-loongarch64-linux-va-fixed-overflow: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	bash tests/compiler/c0/probe-loongarch64-linux-va-fixed-overflow.sh

check-fast: check-loongarch64-linux-va-fixed-overflow

.PHONY: check-static-const-source-boundaries
check-static-const-source-boundaries: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-static-const-source-boundaries.sh

.PHONY: check-x86-reachable-vma-assert
check-x86-reachable-vma-assert: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/probe-x86-reachable-vma-assert.sh

check-fast: check-packed-zero-width-bitfield-barrier
check-fast: check-binary128-literal-oracle check-long-double-literal-precision
check-fast: check-core-ir-shapes check-core-ir-shapes-source check-core-type-verifier check-rv64-coretype-target-admission
check-fast: check-x86-reachable-vma-assert check-static-const-source-boundaries check-aarch64-fixmap-cross-static-const check-rv64-pic-elf-nonpreemptible check-elf-hidden-undefined-pic-source check-x86-vdso-hidden-unused check-x86-reachable-gnu-runtime check-gnu-if-condition-local-owner check-direct-lp64-reachable-source-labels check-static-local-owner-reachability check-gnu-runtime-if-effect-facts check-gnu-builtin-constant-p-if-guard check-asm-operand-plan check-x86-linux-control-state check-linux-three-arch-trap-state check-x86-linux-uaccess-and-abi check-linux-symbolic-integer-and-record check-packed-bitfield-field-layout check-target-selection-m0 check-x86_64-m1 check-token-model check-lexer check-type check-record check-semantic-snapshot check-type-alias check-layout check-rv64-abi check-static-functions check-unsigned-declarations check-long-types check-for-loops check-unbounded-for-break check-prefix-decrement-update check-cast-expressions check-unsigned-char-layout check-pointer-subscripts check-pointer-arithmetic check-pointer-object-const check-const-locals check-global-objects check-bitwise-xor check-integer-bit-operations check-pointer-members check-expression-statements check-postfix-subscripts check-compound-xor-assignment $(MINIC_BINARY) $(MINIAS_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" \
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	HOST_CC="$(CC)" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/run.sh

check-fast: check-function-diagnostic-attribute check-gnu-constant-bitfield-helper-cfg check-gnu-linux-kmalloc-index-bool-guard check-gnu-linux-i915-irq-known-zero check-gnu-linux-bpf-union-zero check-gnu-linux-task-mmu-swp-pfn-guard check-gnu-linux-kaslr-addr-range-guard check-gnu-linux-nf-conntrack-size check-gnu-linux-nvme-prp-guard check-linux-empty-static-stub-cfi check-gnu-linux-static-field-guard check-core-same-global-address-order check-core-compound-record-byte-layout check-core-literal-rhs-snapshot check-core-rhs-no-cfg-snapshot check-gnu-linux-zoned-constant-switch check-gnu-const-global-record check-gnu-dead-file-static-object-graph check-core-inline-statement-constant-guard check-core-field-get-contiguous-mask-guard check-aarch64-fixmap-opaque-asm-clone check-aarch64-weak-direct-call-got check-minic-linux-quick-replay check-linux-gnu-cache-verify check-loongarch64-record-chunk-native-word check-loongarch64-immediate-core-fold check-gnu-linux-kbuild-disjoint-bitmask-assert

.PHONY: check-minic-linux-quick-replay
check-minic-linux-quick-replay: $(MINIC_CC_BINARY)
	python3 tests/compiler/c0/test-minic-linux-quick-replay.py --minic "$(abspath $(MINIC_CC_BINARY))"

.PHONY: check-linux-gnu-cache-verify
check-linux-gnu-cache-verify:
	python3 tools/ci/test_linux_three_isa_gnu_cache_verify_v1.py

.PHONY: check-loongarch64-record-chunk-native-word
check-loongarch64-record-chunk-native-word: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-loongarch64-record-chunk-native-word.sh

.PHONY: check-aarch64-weak-direct-call-got
check-aarch64-weak-direct-call-got: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-aarch64-weak-direct-call-got.sh

.PHONY: check-core-rhs-no-cfg-snapshot
check-core-rhs-no-cfg-snapshot: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/probe-core-rhs-no-cfg-snapshot.sh

.PHONY: check-core-field-get-contiguous-mask-guard
check-core-field-get-contiguous-mask-guard: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/probe-core-field-get-contiguous-mask-guard.sh

.PHONY: check-gnu-dead-file-static-object-graph
check-gnu-dead-file-static-object-graph: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-gnu-dead-file-static-object-graph.sh

.PHONY: check-core-module-reachability-closure
check-core-module-reachability-closure: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" CC="$(CC)" \
	sh tests/compiler/c0/probe-core-module-reachability-closure.sh

# One acceptance surface covers the complete Core+Module reachability owner,
# including the historical GNU ASM/static-label errors and pure Core CFG.
check-fast: check-core-module-reachability-closure

.PHONY: check-core-inline-statement-constant-guard
check-core-inline-statement-constant-guard: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/probe-core-inline-statement-constant-guard.sh

.PHONY: check-aarch64-fixmap-opaque-asm-clone
check-aarch64-fixmap-opaque-asm-clone: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-aarch64-fixmap-opaque-asm-clone.sh

.PHONY: check-aarch64-fixmap-cross-static-const-bug-asm
check-aarch64-fixmap-cross-static-const-bug-asm: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-aarch64-fixmap-cross-static-const-bug-asm.sh

# Keep source-owned BUG table sections and clone behavior in the fast gate.
check-fast: check-aarch64-fixmap-cross-static-const-bug-asm

.PHONY: check-aarch64-typed-extended-float-abi
check-aarch64-typed-extended-float-abi: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-aarch64-typed-extended-float-abi.sh

check-fast: check-aarch64-typed-extended-float-abi

.PHONY: check-typed-extended-float-runtime
check-typed-extended-float-runtime: $(MINIC_CC_BINARY)
	mkdir -p "$(BUILD_DIR)/tests"
	TMPDIR="$(abspath $(BUILD_DIR)/tests)" \
	  bash tests/compiler/c0/probe-typed-extended-float-runtime.sh \
	  "$(abspath $(MINIC_CC_BINARY))"

# The same typed binary128 Core value and runtime ABI must reach every
# production backend; this short probe also runs the x86 native differential.
check-fast: check-typed-extended-float-runtime

.PHONY: check-loongarch64-immediate-core-fold
check-loongarch64-immediate-core-fold: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/probe-loongarch64-immediate-core-fold.sh

.PHONY: check-gnu-const-global-record
check-gnu-const-global-record: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/probe-gnu-const-global-record.sh

.PHONY: check-linux-empty-static-stub-cfi
check-linux-empty-static-stub-cfi: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/probe-linux-empty-static-stub-cfi.sh

.PHONY: check-gnu-linux-static-field-guard
check-gnu-linux-static-field-guard: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	sh tests/compiler/c0/probe-gnu-linux-static-field-guard.sh

.PHONY: check-core-same-global-address-order
check-core-same-global-address-order: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/probe-core-same-global-address-order.sh

.PHONY: check-core-compound-record-byte-layout
check-core-compound-record-byte-layout: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/probe-core-compound-record-byte-layout.sh

.PHONY: check-core-literal-rhs-snapshot
check-core-literal-rhs-snapshot: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/compiler/c0/probe-core-literal-rhs-snapshot.sh

.PHONY: check-gnu-linux-zoned-constant-switch
check-gnu-linux-zoned-constant-switch: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" CC="$(CC)" \
	sh tests/compiler/c0/probe-gnu-linux-zoned-constant-switch.sh

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

# Current 47-TU compiler self-host B1: genuinely executable local gate.
# Both scripts are identical to the current CI opt-in Stage0/1/2 contract.
# These scripts build a *release* Stage0 compiler (irrespective of MODE), then
# run Stage1/Stage2 under qemu-riscv64 with GNU preprocess/asm/link oracles.
# This does not establish Stage2 compilation of Linux or remove GNU tools.
bootstrap:
	python3 tools/ci/minic-bootstrap-current-b1-seed-v1.py \
		--work "$(abspath $(BUILD_DIR))/bootstrap-current-b1"
	python3 tools/ci/minic-bootstrap-current-b1-stage2-v1.py \
		--work "$(abspath $(BUILD_DIR))/bootstrap-current-b1" --workers 4

bootstrap-compare: bootstrap
	@printf '%s\n' \
		"bootstrap-compare: Stage0/1/2 exact source-assembly and loadable-image fixed point PASS" \
		"bootstrap-compare: Stage2 real-program/Linux gates remain independent"

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
		"MINIC_CC_BINARY=$(MINIC_CC_BINARY)" \
		"MINIC_BINARY=$(MINIC_BINARY)"

clean:
	@rm -rf "$(BUILD_DIR)"

distclean:
	@rm -rf build

-include $(MINIC_OBJECTS:.o=.d)
-include $(MINIAS_OBJECTS:.o=.d)
-include $(MINIPP_OBJECTS:.o=.d)
-include $(MINIAR_OBJECTS:.o=.d)
-include $(MINIELF_OBJECTS:.o=.d)
-include $(MINILD_OBJECTS:.o=.d)
-include $(MININM_OBJECTS:.o=.d)
-include $(MINIOBJCOPY_OBJECTS:.o=.d)
-include $(MINISTRIP_OBJECTS:.o=.d)
-include $(TOKEN_MODEL_TEST_OBJECTS:.o=.d)
-include $(LEXER_TEST_OBJECTS:.o=.d)
-include $(TYPE_TEST_OBJECTS:.o=.d)
-include $(RECORD_TEST_OBJECTS:.o=.d)
-include $(SEMANTIC_SNAPSHOT_TEST_OBJECTS:.o=.d)
-include $(TYPE_ALIAS_TEST_OBJECTS:.o=.d)
-include $(AST_CONTRACT_TEST_OBJECTS:.o=.d)
-include $(LAYOUT_TEST_OBJECTS:.o=.d)
-include $(RV64_ABI_TEST_OBJECTS:.o=.d)
-include $(FUNCTION_DIAGNOSTIC_TEST_OBJECT:.o=.d)
-include $(BACKEND_MODULE_ARENA_TEST_OBJECT:.o=.d)

# Cross-target semantics/options smoke without the GNU QEMU toolchains.
.PHONY: check-minic-target-abi-options
check-minic-target-abi-options: $(MINIC_BINARY) $(MINIC_CC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	bash tests/compiler/c0/probe-target-language-abi-options.sh

.PHONY: check-rv64-lp64d-union-fp-abi
check-rv64-lp64d-union-fp-abi: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-rv64-lp64d-union-fp-abi.sh

check-fast: check-rv64-lp64d-union-fp-abi

# RV64 GlobalEmissionPlan migration: the source/differential gate does not
# need cross GNU tools and is included in check-fast. The separate opt-in
# ELF/QEMU gate fails closed (BLOCKED/2) when cross tools are unavailable.
.PHONY: check-rv64-global-plan-source check-rv64-global-plan-gnu-elf
check-rv64-global-plan-source: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	  sh tests/compiler/c0/run-rv64-global-plan-abi.sh

check-fast: check-rv64-global-plan-source

check-rv64-global-plan-gnu-elf: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	  MINIC_RV64_GLOBAL_GNU_ELF=1 sh tests/compiler/c0/run-rv64-global-plan-abi.sh

# RV32 GlobalEmissionPlan migration: same GNU-preprocessed C -> MiniC .s
# source gate (optionally byte-diff C39), plus a strict GNU ELF32/QEMU target.
# Source is short and requires no cross tools; the full gate fails BLOCKED/2
# when GNU RV32 as/ld/readelf or qemu-riscv32 is missing.
.PHONY: check-rv32-global-plan-source check-rv32-global-plan-gnu-elf
check-rv32-global-plan-source: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	  sh tests/compiler/c0/run-rv32-global-plan-abi.sh

check-fast: check-rv32-global-plan-source

check-rv32-global-plan-gnu-elf: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	  MINIC_RV32_GLOBAL_GNU_ELF=1 sh tests/compiler/c0/run-rv32-global-plan-abi.sh

# Shared Linux static-key ASM source/Core grammar; A64 and Loong keep their
# own exact %c0/%0, branch spelling and ELF metadata emission.
.PHONY: check-common-linux-jump-label-contract
check-common-linux-jump-label-contract:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) $(MINIC_CFLAGS) $(MINIC_INCLUDES) \
	  tests/target/common_linux_jump_label_contract.c \
	  -o "$(BUILD_DIR)/tests/common-linux-jump-label-contract"
	"$(BUILD_DIR)/tests/common-linux-jump-label-contract"

check-fast: check-common-linux-jump-label-contract

# Thin target-owned MIR data model and whole-function verifier. Production
# backends will migrate complete function paths after the C/Core boundary is
# closed; this contract test does not imply that migration has occurred.
.PHONY: check-machine-ir-contract
check-machine-ir-contract:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  tests/target/machine_ir_contract_test.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  -o "$(BUILD_DIR)/tests/machine-ir-contract"
	"$(BUILD_DIR)/tests/machine-ir-contract"

check-fast: check-machine-ir-contract

.PHONY: check-machine-core-coverage
check-machine-core-coverage:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror $(MINIC_INCLUDES) \
	  tests/target/machine_core_coverage_test.c \
	  src/target/common/machine_core_coverage.c \
	  -o "$(BUILD_DIR)/tests/machine-core-coverage"
	"$(BUILD_DIR)/tests/machine-core-coverage"

check-fast: check-machine-core-coverage

.PHONY: check-machine-production-preflight
check-machine-production-preflight:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror $(MINIC_INCLUDES) \
	  tests/target/machine_production_preflight_test.c \
	  src/core/core_type.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_core_coverage.c \
	  src/target/common/machine_gnu_asm.c \
	  -o "$(BUILD_DIR)/tests/machine-production-preflight"
	"$(BUILD_DIR)/tests/machine-production-preflight"

check-fast: check-machine-production-preflight

.PHONY: check-machine-source-asm-template
check-machine-source-asm-template:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror $(MINIC_INCLUDES) \
	  tests/target/machine_source_asm_template_test.c \
	  src/target/common/machine_source_asm_template.c \
	  -o "$(BUILD_DIR)/tests/machine-source-asm-template"
	"$(BUILD_DIR)/tests/machine-source-asm-template"

check-fast: check-machine-source-asm-template

.PHONY: check-machine-gnu-asm
check-machine-gnu-asm:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  tests/target/machine_gnu_asm_test.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_gnu_asm.c \
	  -o "$(BUILD_DIR)/tests/machine-gnu-asm"
	"$(BUILD_DIR)/tests/machine-gnu-asm"

check-fast: check-machine-gnu-asm

# Real target-owned physical opcode contracts. These exercise the typed
# builder/verifier/GNU body writer, not the old Core-to-FILE* backends.
.PHONY: check-riscv-machine-gnu-asm
check-riscv-machine-gnu-asm:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  tests/target/riscv_machine_gnu_asm_test.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_gnu_asm.c \
	  src/target/riscv/machine_opcode.c \
	  src/target/riscv/machine_gnu_asm.c \
	  -o "$(BUILD_DIR)/tests/riscv-machine-gnu-asm"
	"$(BUILD_DIR)/tests/riscv-machine-gnu-asm"

check-fast: check-riscv-machine-gnu-asm

.PHONY: check-direct-machine-gnu-asm
check-direct-machine-gnu-asm:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  tests/target/direct_machine_gnu_asm_test.c \
	  src/target/data_layout.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_gnu_asm.c \
	  src/target/common/machine_source_asm_template.c \
	  src/core/core_ir.c \
	  src/core/core_type.c \
	  src/core/core_ir_verify.c \
	  src/target/aarch64/machine_opcode.c \
	  src/target/aarch64/machine_gnu_asm.c \
	  src/target/aarch64/machine_source_asm.c \
	  src/target/aarch64/asm_contract.c \
	  src/target/aarch64/machine_sysreg_contract.c \
	  src/target/aarch64/machine_lse_source_select.c \
	  src/target/loongarch64/machine_opcode.c \
	  src/target/loongarch64/machine_gnu_asm.c \
	  src/target/loongarch64/machine_asm_form.c \
	  src/target/loongarch64/simd_asm.c \
	  src/target/loongarch64/machine_source_asm.c \
	  -o "$(BUILD_DIR)/tests/direct-machine-gnu-asm"
	"$(BUILD_DIR)/tests/direct-machine-gnu-asm"

check-fast: check-direct-machine-gnu-asm

# Five Linux-target Core->MachineIR migration: historical pre-MIR byte goldens
# remain as an archive; each switched target must instead prove the real typed
# output inode and semantic shapes. The current mixed-target gate separates
# these facts explicitly, without permitting legacy fallback within a target.
.PHONY: check-a64-loong-mir-golden check-a64-loong-mir-production \
        check-five-machine-mir-golden check-five-machine-mir-production \
        check-loong-machine-mir-production check-mir-migration-current \
        check-x86-machine-runtime
check-a64-loong-mir-golden: $(MINIC_CC_BINARY)
	python3 tests/compiler/mir-migration/probe_machine_ir.py \
	  --mode golden --minic-cc "$(abspath $(MINIC_CC_BINARY))" \
	  --build-dir "$(abspath $(BUILD_DIR))" --host-cc "$(CC)"

check-five-machine-mir-golden: $(MINIC_CC_BINARY)
	python3 tests/compiler/mir-migration/probe_machine_ir.py \
	  --mode golden --targets five --minic-cc "$(abspath $(MINIC_CC_BINARY))" \
	  --build-dir "$(abspath $(BUILD_DIR))" --host-cc "$(CC)"

check-a64-loong-mir-production: $(MINIC_CC_BINARY)
	python3 tests/compiler/mir-migration/probe_machine_ir.py \
	  --mode production --minic-cc "$(abspath $(MINIC_CC_BINARY))" \
	  --build-dir "$(abspath $(BUILD_DIR))" --host-cc "$(CC)"

check-five-machine-mir-production: $(MINIC_CC_BINARY)
	python3 tests/compiler/mir-migration/probe_machine_ir.py \
	  --mode production --targets five \
	  --minic-cc "$(abspath $(MINIC_CC_BINARY))" \
	  --build-dir "$(abspath $(BUILD_DIR))" --host-cc "$(CC)"

check-loong-machine-mir-production: $(MINIC_CC_BINARY)
	python3 tests/compiler/mir-migration/probe_machine_ir.py \
	  --mode production --targets loongarch64 \
	  --minic-cc "$(abspath $(MINIC_CC_BINARY))" \
	  --build-dir "$(abspath $(BUILD_DIR))" --host-cc "$(CC)"
	bash tests/compiler/mir-migration/probe_loong_llsc_extended_forms.sh \
	  "$(abspath $(MINIC_CC_BINARY))"

# RV32 production is now one live typed writer with no old golden fallback.
# The remaining legacy targets still require their frozen-byte contracts;
# the all-five production gate is the final no-legacy milestone.
check-mir-migration-current: $(MINIC_CC_BINARY)
	python3 tests/compiler/mir-migration/probe_machine_ir.py \
	  --mode production --targets riscv32 \
	  --minic-cc "$(abspath $(MINIC_CC_BINARY))" \
	  --build-dir "$(abspath $(BUILD_DIR))" --host-cc "$(CC)"
	@for arch in riscv64 x86_64 aarch64; do \
	  python3 tests/compiler/mir-migration/probe_machine_ir.py \
	    --mode golden --targets "$$arch" \
	    --minic-cc "$(abspath $(MINIC_CC_BINARY))" \
	    --build-dir "$(abspath $(BUILD_DIR))" --host-cc "$(CC)" || exit; \
	done
	$(MAKE) MODE=$(MODE) BUILD_DIR=$(BUILD_DIR) \
	  check-loong-machine-mir-production

# Small executable oracle: compare GCC's native binary to MiniC assembly
# linked by that same GCC. This is not a five-target or Linux runtime claim.
check-x86-machine-runtime: $(MINIC_CC_BINARY)
	python3 tests/compiler/mir-migration/probe_x86_native.py \
	  --minic-cc "$(abspath $(MINIC_CC_BINARY))" \
	  --build-dir "$(abspath $(BUILD_DIR))" --host-cc "$(CC)"

.PHONY: check-a64-machine-mir-frame-select
check-a64-machine-mir-frame-select:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  -ffunction-sections -fdata-sections -Wl,--gc-sections \
	  tests/target/aarch64/machine_frame_select_test.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_gnu_asm.c \
	  src/target/data_layout.c \
	  src/target/aarch64/machine_frame_select.c \
	  src/target/aarch64/frame_emit.c \
	  src/target/aarch64/machine_opcode.c \
	  src/target/aarch64/machine_gnu_asm.c \
	  src/target/aarch64/machine_source_asm.c \
	  src/target/aarch64/asm_contract.c \
	  src/target/aarch64/machine_sysreg_contract.c \
	  src/target/aarch64/machine_lse_source_select.c \
	  src/target/common/machine_source_asm_template.c \
	  -o "$(BUILD_DIR)/tests/a64-machine-mir-frame-select"
	"$(BUILD_DIR)/tests/a64-machine-mir-frame-select"

.PHONY: check-loong-machine-mir-select
check-loong-machine-mir-select:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  -ffunction-sections -fdata-sections -Wl,--gc-sections \
	  tests/target/loongarch64/machine_select_test.c \
	  src/target/data_layout.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_gnu_asm.c \
	  src/target/common/machine_source_asm_template.c \
	  src/target/loongarch64/machine_opcode.c \
	  src/target/loongarch64/machine_gnu_asm.c \
	  src/target/loongarch64/machine_asm_form.c \
	  src/target/loongarch64/machine_source_asm.c \
	  src/target/loongarch64/machine_select.c \
	  src/target/loongarch64/machine_builtin_select.c \
	  src/target/loongarch64/machine_llsc_select.c \
	  src/target/loongarch64/machine_llsc_extended_select.c \
	  src/target/loongarch64/machine_pte_select.c \
	  src/target/loongarch64/machine_csr_source_select.c \
	  src/target/loongarch64/machine_scalar_source_select.c \
	  src/target/loongarch64/machine_uaccess_select.c \
	  -o "$(BUILD_DIR)/tests/loong-machine-mir-select"
	"$(BUILD_DIR)/tests/loong-machine-mir-select"

# The 25 sealed LoongArch source builtins map to 22 *native* typed opcodes.
# Check physical operands, CSR imm14 and GNU printing without a CALL/BL shim.
.PHONY: check-loong-machine-builtin-opcode
check-loong-machine-builtin-opcode:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  -ffunction-sections -fdata-sections -Wl,--gc-sections \
	  tests/target/loongarch64/machine_builtin_opcode_test.c \
	  src/target/data_layout.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_gnu_asm.c \
	  src/target/common/machine_source_asm_template.c \
	  src/target/loongarch64/machine_opcode.c \
	  src/target/loongarch64/machine_gnu_asm.c \
	  src/target/loongarch64/machine_asm_form.c \
	  src/target/loongarch64/machine_source_asm.c \
	  -o "$(BUILD_DIR)/tests/loong-machine-builtin-opcode"
	"$(BUILD_DIR)/tests/loong-machine-builtin-opcode"

.PHONY: check-a64-machine-core-select
check-a64-machine-core-select:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  -ffunction-sections -fdata-sections -Wl,--gc-sections \
	  tests/target/aarch64/machine_core_select_test.c \
	  src/core/core_ir.c src/core/core_type.c \
	  src/core/core_ir_verify.c src/core/module_ir.c \
	  src/target/data_layout.c \
	  src/target/target_info.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_gnu_asm.c \
	  src/target/common/machine_core_coverage.c \
	  src/target/common/machine_source_asm_template.c \
	  src/target/common/lp64_scalar.c \
	  src/target/common/lp64_frame.c \
	  src/target/common/lp64_aggregate.c \
	  src/target/common/aggregate_shape.c \
	  src/target/common/core_numeric_shape.c \
	  src/target/common/direct_instruction_semantics.c \
	  src/target/common/direct_call_plan.c \
	  src/target/common/core_call_view.c \
	  src/target/common/core_call_index.c \
	  src/target/common/direct_residual_plan.c \
	  src/target/common/elf_module_symbol_info.c \
	  src/target/aarch64/abi.c \
	  src/target/aarch64/machine_opcode.c \
	  src/target/aarch64/machine_gnu_asm.c \
	  src/target/aarch64/machine_source_asm.c \
	  src/target/aarch64/asm_contract.c \
	  src/target/aarch64/machine_sysreg_contract.c \
	  src/target/aarch64/machine_lse_source_select.c \
	  src/target/aarch64/machine_frame_select.c \
	  src/target/aarch64/machine_core_select.c \
	  src/target/aarch64/frame_emit.c \
	  src/target/aarch64/neon_emit.c \
	  -o "$(BUILD_DIR)/tests/a64-machine-core-select"
	"$(BUILD_DIR)/tests/a64-machine-core-select"

.PHONY: check-loong-machine-source-asm
check-loong-machine-source-asm:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  -ffunction-sections -fdata-sections -Wl,--gc-sections \
	  tests/target/loong_machine_source_asm_test.c \
	  src/target/loongarch64/machine_source_asm.c \
	  src/target/loongarch64/machine_asm_form.c \
	  src/target/common/machine_source_asm_template.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  -o "$(BUILD_DIR)/tests/loong-machine-source-asm"
	"$(BUILD_DIR)/tests/loong-machine-source-asm"

.PHONY: check-loong-machine-core-select
check-loong-machine-core-select:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror $(MINIC_INCLUDES) \
	  -ffunction-sections -fdata-sections -Wl,--gc-sections \
	  tests/target/loongarch64/machine_core_select_test.c \
	  src/core/core_ir.c src/core/core_type.c src/core/core_ir_verify.c \
	  src/core/module_ir.c src/target/data_layout.c \
	  src/target/target_info.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_gnu_asm.c \
	  src/target/common/machine_source_asm_template.c \
	  src/target/common/machine_core_coverage.c \
	  src/target/common/lp64_scalar.c \
	  src/target/common/lp64_frame.c \
	  src/target/common/lp64_aggregate.c \
	  src/target/common/aggregate_shape.c \
	  src/target/common/core_numeric_shape.c \
	  src/target/common/direct_instruction_semantics.c \
	  src/target/common/direct_residual_plan.c \
	  src/target/common/direct_call_plan.c \
	  src/target/common/core_call_view.c \
	  src/target/common/core_call_index.c \
	  src/target/common/elf_module_symbol_info.c \
	  src/target/loongarch64/machine_opcode.c \
	  src/target/loongarch64/machine_gnu_asm.c \
	  src/target/loongarch64/machine_asm_form.c \
	  src/target/loongarch64/machine_source_asm.c \
	  src/target/loongarch64/machine_select.c \
	  src/target/loongarch64/machine_builtin_select.c \
	  src/target/loongarch64/machine_llsc_select.c \
	  src/target/loongarch64/machine_llsc_extended_select.c \
	  src/target/loongarch64/machine_pte_select.c \
	  src/target/loongarch64/machine_csr_source_select.c \
	  src/target/loongarch64/machine_scalar_source_select.c \
	  src/target/loongarch64/machine_uaccess_select.c \
	  -o "$(BUILD_DIR)/tests/loong-machine-core-select"
	"$(BUILD_DIR)/tests/loong-machine-core-select"

.PHONY: check-a64-machine-source-asm
check-a64-machine-source-asm:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  tests/target/aarch64/machine_source_asm_test.c \
	  src/target/aarch64/machine_source_asm.c \
	  src/target/aarch64/asm_contract.c \
	  src/target/aarch64/machine_sysreg_contract.c \
	  src/target/aarch64/machine_lse_source_select.c \
	  src/target/common/machine_source_asm_template.c \
	  -o "$(BUILD_DIR)/tests/a64-machine-source-asm"
	"$(BUILD_DIR)/tests/a64-machine-source-asm"

.PHONY: check-x86-machine-frame-select
check-x86-machine-frame-select:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  -ffunction-sections -fdata-sections -Wl,--gc-sections \
	  tests/target/x86_64/machine_frame_select_test.c \
	  src/target/data_layout.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_gnu_asm.c \
	  src/target/x86_64/machine_opcode.c \
	  src/target/x86_64/machine_gnu_asm.c \
	  src/target/x86_64/machine_frame_select.c \
	  -o "$(BUILD_DIR)/tests/x86-machine-frame-select"
	"$(BUILD_DIR)/tests/x86-machine-frame-select"

.PHONY: check-riscv-machine-frame-select
check-riscv-machine-frame-select:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror $(MINIC_INCLUDES) \
	  tests/target/riscv_machine_frame_select_test.c \
	  src/target/riscv/machine_frame_select.c \
	  src/target/riscv/machine_opcode.c \
	  src/target/riscv/machine_gnu_asm.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_gnu_asm.c \
	  -o "$(BUILD_DIR)/tests/riscv-machine-frame-select"
	"$(BUILD_DIR)/tests/riscv-machine-frame-select"

.PHONY: check-riscv-machine-core-select
check-riscv-machine-core-select:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  -ffunction-sections -fdata-sections -Wl,--gc-sections \
	  tests/target/riscv_machine_core_select_test.c \
	  src/core/core_ir.c src/core/core_type.c src/core/core_ir_verify.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_gnu_asm.c \
	  src/target/common/machine_core_coverage.c \
	  src/target/common/core_call_view.c \
	  src/target/common/core_call_index.c \
	  src/target/common/aggregate_shape.c \
	  src/target/common/elf_module_symbol_info.c \
	  src/target/riscv32/rv32_abi_plan.c \
	  src/target/riscv32/rv32_abi_cursor.c \
	  src/target/riscv32/rv32_scalar.c \
	  src/target/riscv64/abi.c \
	  src/target/riscv64/function_abi_plan.c \
	  src/target/riscv64/frame_requirements.c \
	  src/target/riscv/machine_core_select.c \
	  src/target/riscv/machine_frame_select.c \
	  src/target/riscv/machine_opcode.c \
	  src/target/riscv/machine_gnu_asm.c \
	  -o "$(BUILD_DIR)/tests/riscv-machine-core-select"
	"$(BUILD_DIR)/tests/riscv-machine-core-select"

.PHONY: check-x86-machine-core-select
check-x86-machine-core-select:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror $(MINIC_INCLUDES) \
	  -ffunction-sections -fdata-sections -Wl,--gc-sections \
	  tests/target/x86_64/machine_core_select_test.c \
	  src/core/core_ir.c src/core/core_type.c src/core/core_ir_verify.c \
	  src/core/module_ir.c src/target/data_layout.c \
	  src/target/target_info.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_gnu_asm.c \
	  src/target/common/machine_source_asm_template.c \
	  src/target/common/machine_core_coverage.c \
	  src/target/common/lp64_scalar.c \
	  src/target/common/lp64_frame.c \
	  src/target/common/aggregate_shape.c \
	  src/target/common/core_numeric_shape.c \
	  src/target/common/direct_instruction_semantics.c \
	  src/target/common/direct_residual_plan.c \
	  src/target/common/elf_module_symbol_info.c \
	  src/target/x86_64/abi.c \
	  src/target/x86_64/asm_contract.c \
	  src/target/x86_64/machine_raid6_source_policy.c \
	  src/target/x86_64/machine_opcode.c \
	  src/target/x86_64/machine_gnu_asm.c \
	  src/target/x86_64/machine_frame_select.c \
	  src/target/x86_64/machine_core_select.c \
	  -o "$(BUILD_DIR)/tests/x86-machine-core-select"
	"$(BUILD_DIR)/tests/x86-machine-core-select"
	TMPDIR="$(abspath $(BUILD_DIR))/tests" \
	  bash tests/target/x86_64/check_machine_fp_differential.sh \
	  "$(abspath $(BUILD_DIR))/tests/x86-machine-core-select"

# Structural parity is a focused, native-host gate: all five target selectors
# must accept their proven Core subsets, reject unsupported whole functions,
# and satisfy Core-origin/effect coverage before the production path is switched.
# Keep Linux and expensive full-compiler suites separate from this gate.
.PHONY: check-five-machine-core-select
check-five-machine-core-select: check-machine-ir-contract \
	check-machine-core-coverage check-machine-source-asm-template \
	check-riscv-machine-core-select check-x86-machine-core-select \
	check-a64-machine-core-select check-loong-machine-core-select \
	check-loong-machine-builtin-opcode

.PHONY: check-x86-machine-gnu-asm
check-x86-machine-gnu-asm:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  tests/target/x86_machine_gnu_asm_test.c \
	  src/target/data_layout.c \
	  src/target/common/machine_ir.c \
	  src/target/common/machine_builder.c \
	  src/target/common/machine_ir_verify.c \
	  src/target/common/machine_gnu_asm.c \
	  src/target/common/machine_source_asm_template.c \
	  src/target/x86_64/asm_contract.c \
	  src/target/x86_64/machine_raid6_source_policy.c \
	  src/target/x86_64/machine_opcode.c \
	  src/target/x86_64/machine_gnu_asm.c \
	  -o "$(BUILD_DIR)/tests/x86-machine-gnu-asm"
	"$(BUILD_DIR)/tests/x86-machine-gnu-asm"

check-fast: check-x86-machine-gnu-asm

# Source ASM and local labels are sealed to canonical owned symbol identities
# before Core publication. All five machine consumers validate these names
# without reopening frontend statement tables or interpolating untrusted text.
.PHONY: check-target-sealed-symbol-contract
check-target-sealed-symbol-contract:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  -ffunction-sections -fdata-sections -Wl,--gc-sections \
	  tests/target/target_sealed_symbol_contract.c \
	  src/target/common/module_global_emit.c \
	  src/core/module_ir.c \
	  -o "$(BUILD_DIR)/tests/target-sealed-symbol-contract"
	"$(BUILD_DIR)/tests/target-sealed-symbol-contract"

check-fast: check-target-sealed-symbol-contract

# Core owns the local asm-goto symbol, and frontend cleanup lifetimes are
# enforced at source resolution before these taken edges enter Core CFG.
.PHONY: check-frontend-asm-goto-cleanup-lifetime
check-frontend-asm-goto-cleanup-lifetime: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	  sh tests/frontend/p24_asm_goto_cleanup_regression.sh

check-fast: check-frontend-asm-goto-cleanup-lifetime

# The Core/machine header closure must never drag in the C AST. A standalone
# translation unit catches accidental transitive dependencies before backend
# implementation files can mask them with a frontend include of their own.
.PHONY: check-target-machine-header-boundary
check-target-machine-header-boundary:
	$(CC) -std=c11 $(COMMON_WARNINGS) -Werror -Iinclude -Isrc \
		-fsyntax-only tests/target/machine_header_ast_boundary.c
	@echo "TARGET_MACHINE_HEADER_BOUNDARY=PASS c_ast=absent c_type=absent"

check-fast: check-target-machine-header-boundary

# Target-specific ISA/ABI/mitigation admission is dispatched exclusively
# through the generated target registration table; generic driver code does
# not know per-architecture option spellings.
.PHONY: check-target-machine-options-registry
check-target-machine-options-registry:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -Werror $(MINIC_INCLUDES) \
	  tests/target/machine_options_registry_test.c \
	  src/target/target_catalog.c src/target/target_machine_options.c \
	  src/target/machine_options/riscv.c \
	  src/target/machine_options/aarch64.c \
	  src/target/machine_options/loongarch64.c \
	  src/target/machine_options/x86_64.c \
	  src/target/riscv/isa_resolve.c \
	  -o "$(BUILD_DIR)/tests/target-machine-options-registry"
	"$(BUILD_DIR)/tests/target-machine-options-registry"

check-fast: check-target-machine-options-registry

# x86 Linux structured ASM families retire legacy machine-side template
# dispatch; GNU ELF/native is available on the host, no cross toolchain.
.PHONY: check-x86-typed-five-legacy
check-x86-typed-five-legacy: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	  sh tests/compiler/c0/run-x86-typed-five-legacy-abi.sh

check-fast: check-x86-typed-five-legacy

# R5 x86: six further typed families; GNU native + byte-for-byte R4 parity
# when MINIC_X86_TYPED_BASELINE_CC points to the preceding compiler.
.PHONY: check-x86-typed-six-legacy
check-x86-typed-six-legacy: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	  sh tests/compiler/c0/run-x86-typed-six-legacy-abi.sh

check-fast: check-x86-typed-six-legacy

# Loong Linux descriptor ownership is checked by a real linked machine gate:
# all backend references remain available, but the compiler CLI main is not.
LOONG_TYPED_DESCRIPTOR_TEST_BINARY := $(BUILD_DIR)/tests/target/loongarch64-typed-descriptor
LOONG_TYPED_DESCRIPTOR_TEST_OBJECTS := $(filter-out $(BUILD_DIR)/obj/tools/minic-cc/main.o,$(MINIC_OBJECTS))

.PHONY: check-loong-typed-descriptors
check-loong-typed-descriptors: $(LOONG_TYPED_DESCRIPTOR_TEST_OBJECTS)
	mkdir -p "$(BUILD_DIR)/tests/target"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) \
	  tests/target/loongarch64_typed_descriptor_test.c \
	  $(LOONG_TYPED_DESCRIPTOR_TEST_OBJECTS) $(MINIC_LDFLAGS) \
	  -o "$(LOONG_TYPED_DESCRIPTOR_TEST_BINARY)"
	"$(abspath $(LOONG_TYPED_DESCRIPTOR_TEST_BINARY))"

check-fast: check-loong-typed-descriptors

# The C/GNU ASM admission descriptor must never be part of MinicBackend.
# Both source and machine dispatch use the same target registration inventory.
.PHONY: check-backend-source-registration
check-backend-source-registration: $(LOONG_TYPED_DESCRIPTOR_TEST_OBJECTS)
	mkdir -p "$(BUILD_DIR)/tests/target"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) \
	  tests/target/backend_source_registration_test.c \
	  $(LOONG_TYPED_DESCRIPTOR_TEST_OBJECTS) $(MINIC_LDFLAGS) \
	  -o "$(BUILD_DIR)/tests/target/backend-source-registration"
	"$(abspath $(BUILD_DIR)/tests/target/backend-source-registration)"

check-fast: check-backend-source-registration

# Real AArch64/x86-64 physical ASM emission must depend on sealed CoreType
# alone. The public Core schema no longer retains C descriptors: require
# byte-identical repeated output and reject malformed Core types with zero bytes.
TYPED_ASM_CORE_POISON_TEST_BINARY := $(BUILD_DIR)/tests/target/typed-asm-core-poison

.PHONY: check-typed-asm-core-poison
check-typed-asm-core-poison: $(LOONG_TYPED_DESCRIPTOR_TEST_OBJECTS)
	mkdir -p "$(BUILD_DIR)/tests/target"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) \
	  tests/target/typed_asm_core_poison_test.c \
	  $(LOONG_TYPED_DESCRIPTOR_TEST_OBJECTS) $(MINIC_LDFLAGS) \
	  -o "$(TYPED_ASM_CORE_POISON_TEST_BINARY)"
	"$(abspath $(TYPED_ASM_CORE_POISON_TEST_BINARY))"

check-fast: check-typed-asm-core-poison

# All target-qualified GNU ASM forms must be sealed into a machine-owned
# FormId before Core dispatch; generic structured Core ASM deliberately
# remains ID 0. Test Source admission, direct machine poison, and GNU output.
P45_MACHINE_FORM_TEST_BINARY := $(BUILD_DIR)/tests/target/p45-machine-form-boundary

.PHONY: check-p45-machine-form-boundary
check-p45-machine-form-boundary: $(LOONG_TYPED_DESCRIPTOR_TEST_OBJECTS) $(MINIC_CC_BINARY)
	@mkdir -p "$(BUILD_DIR)/tests/target"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) \
	  tests/target/p45_machine_form_boundary_test.c \
	  $(LOONG_TYPED_DESCRIPTOR_TEST_OBJECTS) $(MINIC_LDFLAGS) \
	  -o "$(P45_MACHINE_FORM_TEST_BINARY)"
	"$(abspath $(P45_MACHINE_FORM_TEST_BINARY))"
	bash tests/compiler/c0/probe-p45-machine-form-trace.sh \
	  "$(abspath $(MINIC_CC_BINARY))"

check-fast: check-p45-machine-form-boundary

# The original 16 AArch64 per-CPU LL/SC variants have named GNU operands.
# Core canonicalization erases tmp/ret name differences: machine variants
# must carry only properties still independently recognizable afterward.
.PHONY: check-p451-a64-percpu-normalized
check-p451-a64-percpu-normalized: $(MINIC_CC_BINARY)
	bash tests/compiler/c0/probe-p451-a64-percpu-normalized.sh \
	  "$(abspath $(MINIC_CC_BINARY))"

check-fast: check-p451-a64-percpu-normalized

# P46 verifies both the five-ISA source output contract and native SysV
# behavior while collapsing target machine copies and Loong text passes.
.PHONY: check-p46-aggregate-and-loong-oracle
check-p46-aggregate-and-loong-oracle: $(MINIC_CC_BINARY)
	bash tests/compiler/c0/probe-p46-oracle.sh \
	  "$(abspath $(MINIC_CC_BINARY))"

# An early x86 kernel .head.text aggregate uses independent source/dest
# physical memory addresses, but retains link VMA for numeric casts.
.PHONY: check-p46-x86-head-aggregate
check-p46-x86-head-aggregate: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	  bash tests/compiler/c0/probe-p46-head-aggregate.sh

check-fast: check-p46-aggregate-and-loong-oracle check-p46-x86-head-aggregate

# The x86 global object plan is an ELF ABI contract, not merely emitted
# source: compare GNU/MiniC native execution, named section types, symbol
# sizes, relocation counts and invalid nonzero .bss initializers.
.PHONY: check-x86-global-plan-abi
check-x86-global-plan-abi: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	  sh tests/compiler/c0/run-x86-global-plan-abi.sh

# Reuse the existing RV32/RV64 source-only targets defined above; the
# separate opt-in cross GNU ELF/QEMU targets retain their BLOCKED semantics.
.PHONY: check-five-isa-global-section-source
check-five-isa-global-section-source: $(MINIC_CC_BINARY)
	@for target in x86_64 aarch64 loongarch64 riscv32 riscv64; do \
	  MINIC_SECTION_ONLY=1 bash tests/compiler/c0/probe-linux-global-section-reloc.sh \
	    "$(abspath $(MINIC_CC_BINARY))" "$$target" || exit $$?; \
	done
	@echo 'P47_FIVE_ISA_GLOBAL_SECTION_SOURCE=PASS targets=5 GNU_ELF=NOT_RUN QEMU=NOT_RUN'

# One focused fail-closed gate for P47's five-ISA ELF global-output boundary.
# P46 exercises GNU AS/LD and real native x86 runtime, while RV32/RV64 and
# the remaining ISAs are source-only until separately certified under QEMU.
.PHONY: check-p47-five-isa-output-smoke
check-p47-five-isa-output-smoke: check-p46-aggregate-and-loong-oracle check-p46-x86-head-aggregate check-x86-global-plan-abi check-rv32-global-plan-source check-rv64-global-plan-source check-five-isa-global-section-source

check-fast: check-x86-global-plan-abi check-five-isa-global-section-source

# One complete P48 x86 early-head memory provenance proof: local pointer
# slots, dynamic offsets, CFG join, aggregate pointer field and opaque alias
# boundary. GNU same-source controls and real ELF displaced native runtime
# must both run; this is not a full Linux QEMU boot certification.
.PHONY: check-p48-x86-head-local-provenance
check-p48-x86-head-local-provenance: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	  bash tests/compiler/c0/probe-p48-head-local-provenance.sh

check-fast: check-p48-x86-head-local-provenance

# P50: one source-to-machine RISC-V vararg alignment ABI regression.
# GNU cross-ELF/QEMU execution is a separate strict existing-workflow gate.
.PHONY: check-p50-riscv-overaligned-varargs
check-p50-riscv-overaligned-varargs: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	bash tests/compiler/c0/probe-riscv-overaligned-varargs.sh

check-fast: check-p50-riscv-overaligned-varargs

# Source -> Core -> normalized-machine ASM proof is a single admission
# contract. Check whole instruction families, including hostile rewrites,
# without requiring cross GNU/QEMU tools or granting opaque source IDs power.
A64_NORMALIZED_SEAL_TEST_BINARY := $(BUILD_DIR)/tests/compiler/c0/a64-normalized-seal

.PHONY: check-a64-normalized-seal
check-a64-normalized-seal: $(LOONG_TYPED_DESCRIPTOR_TEST_OBJECTS)
	@mkdir -p "$(BUILD_DIR)/tests/compiler/c0"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) \
	  tests/compiler/c0/probe-a64-normalized-seal.c \
	  $(LOONG_TYPED_DESCRIPTOR_TEST_OBJECTS) $(MINIC_LDFLAGS) \
	  -o "$(A64_NORMALIZED_SEAL_TEST_BINARY)"
	"$(abspath $(A64_NORMALIZED_SEAL_TEST_BINARY))"

.PHONY: check-loong-scalar-normalized-seal
check-loong-scalar-normalized-seal: $(MINIC_CC_BINARY)
	@mkdir -p "$(BUILD_DIR)/tests/compiler/c0"
	$(CC) $(CPPFLAGS) $(MINIC_INCLUDES) $(MINIC_CFLAGS) \
	  -ffunction-sections -fdata-sections -Wl,--gc-sections \
	  tests/compiler/c0/probe-loong-scalar-normalized-seal.c \
	  src/target/loongarch64/machine_asm_form.c \
	  src/target/loongarch64/asm_contract_source.c $(MINIC_LDFLAGS) \
	  -o "$(BUILD_DIR)/tests/compiler/c0/loong-scalar-normalized-seal"
	"$(abspath $(BUILD_DIR))/tests/compiler/c0/loong-scalar-normalized-seal"
	bash tests/compiler/c0/probe-loong-scalar-source-rank.sh \
	  "$(abspath $(MINIC_CC_BINARY))"

check-fast: check-a64-normalized-seal check-loong-scalar-normalized-seal

# R6 x86: eight further legacy ASM families, including GNU-owned ELF
# .tracedata/.altinstructions and original VDSO/syscall native contracts.
.PHONY: check-x86-typed-eight-legacy
check-x86-typed-eight-legacy: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	  sh tests/compiler/c0/run-x86-typed-eight-legacy-abi.sh

check-fast: check-x86-typed-eight-legacy

# R6 Loong: four INVTLB variants and LL/SC ADD_UNLESS widths. Real GNU
# cross assembler/QEMU remains a separate strict target environment.
.PHONY: check-loong-typed-scalar
check-loong-typed-scalar: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" BUILD_DIR="$(abspath $(BUILD_DIR))" \
	  sh tests/compiler/c0/run-loongarch64-typed-invtlb-add-unless.sh

check-fast: check-loong-typed-scalar

# Array source identity is shared by Frontend, Core ingress and five machine
# emitters. Compile all five targets, compare GNU/MiniC x86 native behavior,
# and require fail-closed rejection for invalid C array types.
.PHONY: check-array-shape-convergence
check-array-shape-convergence: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	  sh tests/compiler/c0/run-array-shape-convergence.sh

check-fast: check-array-shape-convergence

# Array address-of validation must compare *effective* element qualifiers:
# const/volatile on a record lvalue also qualify flexible/incomplete, GNU
# zero-length and fixed array members without changing their stored extent.
.PHONY: check-qualified-array-address-verifier
check-qualified-array-address-verifier: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	  bash tests/compiler/c0/probe-qualified-array-address-verifier.sh

check-fast: check-qualified-array-address-verifier

# C-to-Core function signature ingress, qualified record storage and GNU
# cleanup inline-asm are shared frontend/Core contracts. Keep their GNU
# differential and five-ISA source probes in the regular fast gate.
.PHONY: check-core-qualified-record-function-pointer check-gnu-cleanup-asm-lifetime
check-core-qualified-record-function-pointer: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	  bash tests/compiler/c0/probe-core-qualified-record-function-pointer.sh

check-gnu-cleanup-asm-lifetime: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	  bash tests/compiler/c0/probe-gnu-cleanup-goto-lifetime.sh

# Descriptor-table instructions have a strict 10-byte packed Core record
# memory operand, unlike scalar integer loads/stores; verify true GNU ELF
# emission without trying to execute privileged LIDT/LGDT in user mode.
.PHONY: check-x86-linux-descriptor-table-elf
check-x86-linux-descriptor-table-elf: $(MINIC_CC_BINARY)
	bash tests/compiler/c0/probe-x86-linux-descriptor-table-elf.sh \
	  "$(abspath $(MINIC_CC_BINARY))"

check-fast: check-core-qualified-record-function-pointer \
	check-gnu-cleanup-asm-lifetime check-x86-linux-descriptor-table-elf

# Materialized typedef and fixed local arrays must retain the same C semantics
# across all five emitters. Host GNU provides
# native runtime and 16 independent invalid-input oracles (80 target rejects).
.PHONY: check-array-local-materialization
check-array-local-materialization: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	  sh tests/compiler/c0/run-array-local-materialization.sh

check-fast: check-array-local-materialization

# Record-field typedef and declarator arrays share one complete ArrayType;
# verify static initializer slots, designated rewrites and relocations.
.PHONY: check-static-field-typedef-array
check-static-field-typedef-array: $(MINIC_CC_BINARY)
	MINIC_CC="$(abspath $(MINIC_CC_BINARY))" HOST_CC="$(CC)" \
	  sh tests/compiler/c0/run-static-field-typedef-array.sh

check-fast: check-static-field-typedef-array
