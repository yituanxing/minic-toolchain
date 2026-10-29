#ifndef MINIC_TARGET_TARGET_INFO_H
#define MINIC_TARGET_TARGET_INFO_H

#include "target/data_layout.h"
#include "target/riscv/isa_features.h"
#include "target/target_catalog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Zero is invalid: a newly registered target must specify its cursor. */
typedef enum MinicVariadicScalarReadKind {
    MINIC_VARIADIC_SCALAR_READ_UNSUPPORTED = 0,
    MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
    MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR
} MinicVariadicScalarReadKind;

/* ABI policy, not a Core architecture switch. Copying a structured cursor
 * differs from assigning a scalar cursor; unsupported is always explicit. */
typedef enum MinicVariadicCopyKind {
    MINIC_VARIADIC_COPY_UNSUPPORTED = 0,
    MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
    MINIC_VARIADIC_COPY_ABI_STATE
} MinicVariadicCopyKind;

typedef enum MinicIntegerLiteralBase {
    MINIC_INTEGER_LITERAL_BASE_BINARY = 2,
    MINIC_INTEGER_LITERAL_BASE_OCTAL = 8,
    MINIC_INTEGER_LITERAL_BASE_DECIMAL = 10,
    MINIC_INTEGER_LITERAL_BASE_HEXADECIMAL = 16
} MinicIntegerLiteralBase;

typedef struct MinicTargetIntegerModel {
    MinicIntegerSign plain_char_sign;
    /* Target semantic width used for integer ranges and conversions. Signed
       widths include the sign bit; this is intentionally independent of object
       storage size/alignment and therefore _Bool is one semantic bit on RV64. */
    unsigned int semantic_width_bits[MINIC_INTEGER_RANK_INT128 + 1];
} MinicTargetIntegerModel;

/* These admit C declaration identities only. They do not promise machine
 * arithmetic, storage, varargs marshaling or call/return ABI support. */
typedef enum MinicTargetLanguageCapability {
    MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS = 1U << 0,
    MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS = 1U << 1,
    MINIC_TARGET_LANG_ABI_STATE_VA_LIST_CURSOR = 1U << 2,
    /* Type-only EFI callback ABI marker; does NOT admit actual MS x64 CALL. */
    MINIC_TARGET_LANG_MS_X64_FUNCTION_POINTER_DECLARATIONS = 1U << 3
} MinicTargetLanguageCapability;

/* Code-generation contracts, kept distinct from declaration-only language
 * type capabilities. Target backends explicitly implement each flag. */
typedef enum MinicTargetCallCapability {
    MINIC_TARGET_CALL_MS_X64_FIXED_SCALAR = 1U << 0
} MinicTargetCallCapability;

typedef struct MinicTargetInfo {
    MinicTargetArchitecture architecture;
    const MinicDataLayout *data_layout;
    const MinicTargetIntegerModel *integer_model;
    MinicType wide_character_type;
    /* C identity of libc size_t in canonical runtime builtin prototypes. */
    MinicType libc_size_type;
    MinicIntegerRank word_integer_rank;
    unsigned int language_type_capabilities;
    unsigned int call_lowering_capabilities;
    bool gnu_sizeof_void_is_one;
    bool gnu_sizeof_function_is_one;
    bool call_frame_return_address_level0;
    bool call_frame_frame_address_level0;
    /* Only targets with a persistent caller FP record can unwind level 1. */
    bool call_frame_frame_address_level1;
    MinicVariadicScalarReadKind variadic_scalar_read_kind;
    size_t variadic_linear_slot_bytes;
    MinicVariadicCopyKind variadic_copy_kind;
    /* Target-owned capability: executable/shared ELF symbol references must
     * use qualified local-PCREL or external-GOT lowering and PLT-capable
     * function calls. RV32 remains fail-closed for PIC/PIE. */
    bool pic_lowering_supported;
    /* Per-translation-unit target-qualified GNU PIC/PIE lowering selection.
     * The canonical target descriptors default to static PC-relative.
     * A CLI invocation copies the descriptor before setting this bit. */
    bool position_independent;
    /* RISC-V-specific, per-invocation subtarget. Legacy default preserved. */
    MinicRiscvIsa riscv_isa;
} MinicTargetInfo;

/* Invocation-scoped language ABI overrides. Never mutate a canonical target
 * descriptor or its shared integer_model: two compilations may use different
 * -fsigned-char/-funsigned-char policies in the same process. */
typedef struct MinicTargetInvocation {
    MinicTargetInfo target;
    MinicTargetIntegerModel integer_model;
    MinicType default_wide_character_type;
} MinicTargetInvocation;

bool minic_target_invocation_begin(MinicTargetInvocation *invocation,
                                   const MinicTargetInfo *canonical);
bool minic_target_invocation_plain_char(MinicTargetInvocation *invocation,
                                        MinicIntegerSign sign);
bool minic_target_invocation_short_wchar(MinicTargetInvocation *invocation,
                                         bool enabled);

const MinicTargetInfo *minic_default_target_info(void);
const MinicTargetInfo *minic_target_info_for_architecture(MinicTargetArchitecture architecture);
const MinicTargetInfo *minic_target_info_from_name(const char *name);
const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target);
const MinicTargetIntegerModel *minic_target_info_integer_model(const MinicTargetInfo *target);
/* One admission policy shared by the parser and AST verifier. */
bool minic_target_info_language_type_supported(const MinicTargetInfo *target,
                                               MinicTypeBaseKind kind);
bool minic_target_info_named_builtin_type(const MinicTargetInfo *target,
                                          const char *spelling, size_t length,
                                          MinicType *type);
/* Target-specific GCC unary builtins are described here without making
 * Frontend or Core switch on an architecture. Machine encoding stays owned
 * by the target CALL emitter, with an internal unspellable callee name. */
/* Machine-builtin ABI schema. No frontend ISA switch or invented Core opcode.
 * The same descriptor is rechecked by the final machine CALL emitter.
 * immediate_index is SIZE_MAX when all operands occupy ordinary ABI GPRs. */
bool minic_target_info_machine_builtin_signature(
    const MinicTargetInfo *target, const char *spelling, size_t length,
    char *callee, size_t callee_capacity, MinicType *result,
    MinicType arguments[3], size_t *argument_count,
    const char **mnemonic, size_t *immediate_index);
bool minic_target_info_header_attribute_supported(const MinicTargetInfo *target,
                                                  const char *spelling,
                                                  size_t length);
/* GNU x86 function-security attribute spellings are target-admitted at the
 * language boundary. Argument arity/spellings remain a parser obligation;
 * admission alone never requests an unsupported machine mitigation. */
bool minic_target_info_gnu_function_security_attribute_supported(
    const MinicTargetInfo *target, const char *attribute, size_t length);
/* ELF linkage is a source-program property shared by the two LP64 targets.
 * A proven TU-local symbol may use PC-relative addressing in PIC; all other
 * addresses need a target-specific GOT sequence. */
bool minic_target_info_elf_object_is_tu_local(const MinicC0Program *program,
                                              const char *symbol);
bool minic_target_info_elf_function_is_tu_local(const MinicC0Program *program,
                                                const char *symbol);
bool minic_target_info_function_pointer_calling_convention_declaration_supported(
    const MinicTargetInfo *target, MinicFunctionCallingConvention convention);
bool minic_target_info_function_calling_convention_scalar_call_supported(
    const MinicTargetInfo *target, MinicFunctionCallingConvention convention);
bool minic_target_info_builtin_va_list_type(const MinicTargetInfo *target,
                                            MinicType *type);
bool minic_target_info_variadic_state_copy_type_supported(
    const MinicTargetInfo *, MinicType);
bool minic_target_info_plain_char_sign(const MinicTargetInfo *target, MinicIntegerSign *sign);
bool minic_target_info_plain_char_type(const MinicTargetInfo *target, MinicType *type);
bool minic_target_info_wide_character_type(const MinicTargetInfo *target, MinicType *type);
bool minic_target_info_word_integer_type(const MinicTargetInfo *target,
                                         MinicIntegerSign sign,
                                         MinicType *type);
/* GNU same-TU function aliases may use a target-approved, word-register
 * compatible alias ABI even when source prototypes intentionally differ.
 * The predicate is shared by parse finalization and the AST verifier. */
bool minic_target_info_function_alias_abi_compatible(
    const MinicTargetInfo *target, const MinicC0Program *program,
    const MinicFunction *alias, const MinicFunction *owner);
bool minic_target_info_sizeof_type(const MinicTargetInfo *target,
                                   const MinicC0Program *program,
                                   MinicType type,
                                   size_t *size);
bool minic_target_info_integer_width(const MinicTargetInfo *target,
                                     const MinicC0Program *program,
                                     MinicType type,
                                     unsigned int *bits);
bool minic_target_info_integer_promotion(const MinicTargetInfo *target,
                                         MinicType type,
                                         MinicType *result);
bool minic_target_info_integer_promotion_for_program(const MinicTargetInfo *target,
                                                     const MinicC0Program *program,
                                                     MinicType type,
                                                     MinicType *result);
bool minic_target_info_integer_common(const MinicTargetInfo *target,
                                      MinicType left,
                                      MinicType right,
                                      MinicType *result);
bool minic_target_info_integer_common_for_program(const MinicTargetInfo *target,
                                                  const MinicC0Program *program,
                                                  MinicType left,
                                                  MinicType right,
                                                  MinicType *result);
bool minic_target_info_integer_literal_type(const MinicTargetInfo *target,
                                            MinicIntegerLiteralBase base,
                                            bool has_unsigned_suffix,
                                            unsigned int long_count,
                                            uint64_t value,
                                            MinicType *result);
/* Named register writes are target-described separately from reads;
 * no global SP/TP/RSP mutation may enter the machine emitter. */
/* GNU output operand grammar is not C lvalue mutability and is not
 * target-machine ASM qualification. Targets describe the exact exceptions. */
bool minic_target_info_inline_asm_readwrite_hardreg_supported(
    const MinicTargetInfo *, const char *, size_t,
    const char *, size_t, MinicType);
bool minic_target_info_fixed_register_write_supported(
    const MinicTargetInfo *, const char *, size_t);
bool minic_target_info_fixed_register_supported(const MinicTargetInfo *target,
                                                const char *name,
                                                size_t name_length);
bool minic_target_info_local_fixed_register_supported(const MinicTargetInfo *target,
                                                      const char *name,
                                                      size_t name_length);
/* Syntax-only GNU source clobber, NOT local hard-register permission. */
bool minic_target_info_inline_asm_source_clobber_supported(
    const MinicTargetInfo *target,const char *name,size_t name_length);
bool minic_target_info_inline_asm_register_clobber_supported(const MinicTargetInfo *target,
                                                             const char *name,
                                                             size_t name_length);
bool minic_target_info_inline_asm_immediate_may_defer(const MinicTargetInfo *target);
const char *minic_target_info_inline_asm_immediate_constraint_diagnostic(
    const MinicTargetInfo *target);
bool minic_target_info_inline_asm_immediate_constraint_supported(const MinicTargetInfo *target,
                                                                 const char *constraint,
                                                                 size_t constraint_length,
                                                                 int64_t value);
bool minic_target_info_call_frame_address_supported(const MinicTargetInfo *target,
                                                    MinicCallFrameAddressKind kind,
                                                    unsigned int level);

#endif
