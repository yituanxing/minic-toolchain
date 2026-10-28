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

MINIC_SOURCES := \
	src/compiler/compiler.c \
	src/compiler/core_function_set.c \
	src/compiler/inline_integer.c \
	src/compiler/inline_symbolic.c \
	src/compiler/inline_boolean.c \
	src/compiler/inline_reference.c \
	src/compiler/core_reachability.c \
	src/core/core_ir.c \
	src/core/core_ir_verify.c \
	src/core/core_ir_dump.c \
	src/core/value_uses.c \
	src/core/reentry_index.c \
	src/core/core_lower.c \
	src/core/core_lower_expression.c \
	src/core/core_lower_address.c \
	src/core/core_lower_scalar_values.c \
	src/core/core_lower_bit_fields.c \
	src/core/core_lower_assignments.c \
	src/core/core_lower_record.c \
	src/core/core_lower_varargs.c \
	src/core/core_lower_local_effects.c \
	src/core/core_lower_loop.c \
	src/core/core_lower_if.c \
	src/core/core_lower_switch.c \
	src/core/core_lower_calls.c \
	src/core/core_lower_expr_flow.c \
	src/core/core_lower_expr_scalar.c \
	src/core/core_lower_arithmetic.c \
	src/core/core_lower_const_facts.c \
	src/core/cfg_constant_calls.c \
	src/core/local_facts.c \
	src/core/source_loop_shapes.c \
	src/core/core_lower_bool.c \
	src/core/core_lower_types.c \
	src/core/core_lower_metadata.c \
	src/core/core_lower_asm.c \
	src/core/core_lower_asm_target.c \
	src/core/core_lower_asm_operands.c \
	src/core/core_lower_asm_structured.c \
	src/core/core_lower_asm_scalar_forms.c \
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
	src/frontend/parser_type_query.c \
	src/frontend/parser_typedef.c \
	src/frontend/token.c \
	src/frontend/type.c \
	src/target/data_layout.c \
	src/target/target_info.c \
	src/target/target_catalog.c \
	src/target/backend.c \
	src/target/backend_core_usage.c \
	src/target/common/global_emit.c \
	src/target/common/direct_program_emit.c \
	src/target/common/direct_preflight.c \
	src/target/common/direct_function_profile.c \
	src/target/common/direct_function_plan.c \
	src/target/common/core_function_plan.c \
	src/target/common/core_function_facts.c \
	src/target/common/core_numeric_shape.c \
	src/target/common/core_value_slots.c \
	src/target/common/core_object_storage.c \
	src/target/common/core_call_view.c \
	src/target/common/core_call_index.c \
	src/target/common/direct_call_plan.c \
	src/target/common/direct_instruction_semantics.c \
	src/target/common/direct_residual_plan.c \
	src/target/common/lp64_aggregate.c \
	src/target/common/lp64_scalar.c \
	src/target/common/lp64_frame.c \
	src/target/riscv/isa_resolve.c \
	src/target/riscv64/isa.c \
	src/target/riscv32/rv32_emit.c \
	src/target/riscv32/rv32_integer_emit.c \
	src/target/riscv32/rv32_wide_emit.c \
	src/target/riscv32/rv32_fp_emit.c \
	src/target/riscv32/rv32_memory_emit.c \
	src/target/riscv32/rv32_frame.c \
	src/target/riscv32/rv32_scalar.c \
	src/target/riscv32/rv32_call.c \
	src/target/riscv32/rv32_abi_plan.c \
	src/target/riscv32/rv32_abi_cursor.c \
	src/target/riscv32/rv32_global_emit.c \
	src/target/riscv64/abi.c \
	src/target/riscv64/codegen_support.c \
	src/target/riscv64/core_codegen.c \
	src/target/riscv64/core_admission.c \
	src/target/riscv64/function_abi_plan.c \
	src/target/riscv64/memory_emit.c \
	src/target/riscv/record_copy_bytes.c \
	src/target/riscv/word_operations.c \
	src/target/riscv64/inline_asm_emit.c \
	src/target/riscv64/call_emit.c \
	src/target/riscv64/scalar_emit.c \
	src/target/riscv64/runtime_libcall.c \
	src/target/riscv64/fp_emit.c \
	src/target/riscv64/overflow_emit.c \
	src/target/riscv64/frame_layout.c \
	src/target/riscv64/frame_access.c \
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
	src/target/x86_64/machine_emit.c \
	src/target/x86_64/asm_contract.c \
	src/target/x86_64/machine_asm.c \
	src/target/x86_64/machine_support.c \
	src/target/x86_64/machine_fp.c \
	src/target/x86_64/machine_call.c \
	src/target/x86_64/abi.c \
	src/target/x86_64/call_plan.c \
	src/target/x86_64/frame.c \
	src/target/x86_64/global_emit.c \
	src/target/aarch64/abi.c \
	src/target/aarch64/codegen.c \
	src/target/aarch64/block_emit.c \
	src/target/aarch64/residual_emit.c \
	src/target/aarch64/scalar_normalize.c \
	src/target/aarch64/memory_emit.c \
	src/target/aarch64/call_emit.c \
	src/target/aarch64/integer_emit.c \
	src/target/aarch64/fp_emit.c \
	src/target/aarch64/asm_legalize.c \
	src/target/loongarch64/codegen.c \
	src/target/loongarch64/block_emit.c \
	src/target/loongarch64/residual_emit.c \
	src/target/loongarch64/scalar_normalize.c \
	src/target/loongarch64/memory_emit.c \
	src/target/loongarch64/call_emit.c \
	src/target/loongarch64/integer_emit.c \
	src/target/loongarch64/fp_emit.c \
	src/target/loongarch64/frame_emit.c \
	tools/minic-cc/main.c
MINIC_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(MINIC_SOURCES))
MINIC_CC_BINARY := $(BUILD_DIR)/bin/minic-cc
MINIC_DRIVER_OBJECTS := $(BUILD_DIR)/obj/tools/minic/main.o \
	$(BUILD_DIR)/obj/src/target/target_catalog.o
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
	assembler/src/cfi.c \
	assembler/src/cfi_writer.c \
	assembler/src/debug_line.c \
	assembler/src/riscv_target.c \
	assembler/src/aarch64_target.c \
	assembler/src/x86_64_target.c \
	assembler/src/loongarch64_target.c \
	assembler/src/target_registry.c \
	assembler/src/riscv_profile.c \
	assembler/src/expression.c \
	assembler/src/mc_expr.c \
	assembler/src/mc_lexer.c \
	assembler/src/mc_layout.c \
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

TYPE_ALIAS_TEST_SOURCES := \
	src/frontend/ast.c \
	src/frontend/type.c \
	tests/frontend/type_alias_test.c
TYPE_ALIAS_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(TYPE_ALIAS_TEST_SOURCES))
TYPE_ALIAS_TEST_BINARY  := $(BUILD_DIR)/tests/frontend/type-alias-test

AST_CONTRACT_TEST_SOURCES := \
	src/frontend/ast.c \
	src/frontend/expression_semantics.c \
	src/frontend/ast_global.c \
	src/frontend/ast_traversal.c \
	src/frontend/ast_verifier.c \
	src/frontend/cast_normalization.c \
	src/frontend/type.c \
	src/target/data_layout.c \
	src/target/target_info.c \
	src/target/target_catalog.c \
	tests/frontend/ast_contract_test.c
AST_CONTRACT_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(AST_CONTRACT_TEST_SOURCES))
AST_CONTRACT_TEST_BINARY  := $(BUILD_DIR)/tests/frontend/ast-contract-test

LAYOUT_TEST_SOURCES := \
	src/frontend/ast.c \
	src/frontend/type.c \
	src/target/data_layout.c \
	src/target/target_info.c \
	src/target/target_catalog.c \
	tests/target/riscv64/layout_test.c
LAYOUT_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(LAYOUT_TEST_SOURCES))
LAYOUT_TEST_BINARY  := $(BUILD_DIR)/tests/target/riscv64/layout-test

RV64_ABI_TEST_SOURCES := \
	src/frontend/ast.c \
	src/frontend/ast_global.c \
	src/frontend/type.c \
	src/target/data_layout.c \
	src/target/riscv64/abi.c \
	src/target/riscv64/function_abi_plan.c \
	tests/target/riscv64/abi_test.c
RV64_ABI_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(RV64_ABI_TEST_SOURCES))
RV64_ABI_TEST_BINARY  := $(BUILD_DIR)/tests/target/riscv64/abi-test

.PHONY: runtime-fourarch runtime-fourarch-reference runtime-rv64i check-rv64i-runtime-archive all help prepare check check-fast check-four-backend-lua-frontier check-four-backend-lua-accept check-four-backend-zlib-frontier check-four-backend-zlib-accept check-target-selection-m0 check-four-backend-m39-lp64-bitops check-four-backend-m15-scalar-return check-four-backend-m14-shift check-four-backend-m13-narrow-arith check-four-backend-m12-bitwise check-four-backend-m11-index check-four-backend-m10-mod check-four-backend-m9-unsigned check-four-backend-m8-narrow check-four-backend-m7-frame check-four-backend-m6-address check-four-backend-m5-pointer check-four-backend-m4-global check-four-backend-m38-fp-varargs check-four-backend-m37-fp-arith check-four-backend-m36-lp64-mul check-four-backend-m35-ptr-to-lp64 check-four-backend-m34-fp-call-result check-four-backend-m33-pointer-less check-four-backend-m32-unreachable check-four-backend-m31-float-promote check-four-backend-m30-fp-call check-four-backend-m29-double-cmp check-four-backend-m28-int-double check-four-backend-m27-fp-parameter check-four-backend-m26-double-div check-four-backend-m25-double-transport check-four-backend-m24-indirect-call check-four-backend-m23-lp64-compare check-four-backend-m22-lp64-conv check-four-backend-m21-lp64-add check-four-backend-m20-lp64-abi check-four-backend-m19-ptr-compare check-four-backend-m18-varargs check-four-backend-m17-record check-four-backend-cjson-accept check-four-backend-cjson-frontier check-four-backend-tinyaes-accept check-four-backend-tinyaes-frontier check-four-backend-m3-call check-four-backend-m2-stack check-four-backend-m2 check-four-backend-m1 check-four-target-model-m0 check-x86_64-busybox-all-eligible check-x86_64-busybox-complex-five check-x86_64-busybox-host-link check-elf-constructors check-x86_64-one-gp-sysv check-x86_64-busybox-allyes-discovery check-x86_64-busybox-12 check-x86_64-sqlite-frontier check-x86_64-m1 check-x86_64-tinyaes check-x86_64-cjson check-x86_64-abi-elf check-x86_64-lua-frontier check-x86_64-zlib-frontier check-minias-a0 check-minipp-a0 check-minielf-reader-a0 check-mininm-a0 check-mininm-a1 check-miniobjcopy-a0 check-miniobjcopy-a1 check-minicstrip-a0 check-miniar-a0 check-miniar-a1 check-minild-a0 check-minild-a1 check-minild-a2 check-minild-a3 check-minild-a4 check-minild-a5 check-minild-a6 check-minild-script-a0 check-minild-script-a1 check-token-model check-lexer \
	check-type check-record check-type-alias check-ast-contract check-layout check-rv64-abi \
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

$(MINIC_BINARY): $(MINIC_DRIVER_OBJECTS)
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
	@MINIAS="$(abspath $(MINIAS_BINARY))" \
	BUILD_DIR="$(abspath $(BUILD_DIR))" \
	sh tests/assembler/run-a0.sh || { \
	  echo 'MiniAS A0 failed; replaying focused A0 with shell trace for exact failing command' >&2; \
	  MINIAS="$(abspath $(MINIAS_BINARY))" \
	  BUILD_DIR="$(abspath $(BUILD_DIR))" \
	  sh -x tests/assembler/run-a0.sh; \
	  exit 1; \
	}

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

check-fast: check-target-selection-m0 check-x86_64-m1 check-token-model check-lexer check-type check-record check-type-alias check-ast-contract check-layout check-rv64-abi check-static-functions check-unsigned-declarations check-long-types check-for-loops check-unbounded-for-break check-prefix-decrement-update check-cast-expressions check-unsigned-char-layout check-pointer-subscripts check-pointer-arithmetic check-pointer-object-const check-const-locals check-global-objects check-bitwise-xor check-integer-bit-operations check-pointer-members check-expression-statements check-postfix-subscripts check-compound-xor-assignment $(MINIC_BINARY)
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
-include $(TYPE_ALIAS_TEST_OBJECTS:.o=.d)
-include $(AST_CONTRACT_TEST_OBJECTS:.o=.d)
-include $(LAYOUT_TEST_OBJECTS:.o=.d)
-include $(RV64_ABI_TEST_OBJECTS:.o=.d)

# Cross-target semantics/options smoke without the GNU QEMU toolchains.
.PHONY: check-minic-target-abi-options
check-minic-target-abi-options: $(MINIC_BINARY) $(MINIC_CC_BINARY)
	MINIC="$(abspath $(MINIC_BINARY))" MINIC_CC="$(abspath $(MINIC_CC_BINARY))" \
	bash tests/compiler/c0/probe-target-language-abi-options.sh
