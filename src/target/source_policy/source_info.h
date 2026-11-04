#ifndef MINIC_TARGET_SOURCE_POLICY_SOURCE_INFO_H
#define MINIC_TARGET_SOURCE_POLICY_SOURCE_INFO_H

#include "target/target_info.h"
#include "target/data_layout_source.h"

/* All C/GNU source-language identity, type and admission contracts live here.
 * Including the machine target_info.h never pulls in frontend/type.h. */

typedef enum MinicVaCursorSourceKind {
    MINIC_VA_CURSOR_SOURCE_LINEAR_POINTER = 1,
    MINIC_VA_CURSOR_SOURCE_OPAQUE_RECORD,
    MINIC_VA_CURSOR_SOURCE_OPAQUE_RECORD_ARRAY_ONE
} MinicVaCursorSourceKind;

typedef struct MinicVaCursorSourceShape {
    MinicVaCursorSourceKind kind;
    size_t object_size;
    size_t object_alignment;
} MinicVaCursorSourceShape;

typedef enum MinicIntegerLiteralBase {
    MINIC_INTEGER_LITERAL_BASE_BINARY = 2,
    MINIC_INTEGER_LITERAL_BASE_OCTAL = 8,
    MINIC_INTEGER_LITERAL_BASE_DECIMAL = 10,
    MINIC_INTEGER_LITERAL_BASE_HEXADECIMAL = 16
} MinicIntegerLiteralBase;

typedef struct MinicTargetIntegerModel {
    MinicIntegerSign plain_char_sign;
    unsigned int semantic_width_bits[MINIC_INTEGER_RANK_INT128 + 1U];
} MinicTargetIntegerModel;

/* Source declaration capabilities never imply machine arithmetic or ABI. */
typedef enum MinicTargetLanguageCapability {
    MINIC_TARGET_LANG_EXTENDED_FLOAT_DECLARATIONS = 1U << 0,
    MINIC_TARGET_LANG_OPAQUE_VECTOR_HEADER_DECLARATIONS = 1U << 1,
    MINIC_TARGET_LANG_FIXED_VECTOR128_STORAGE_DECLARATIONS = 1U << 2,
    MINIC_TARGET_LANG_MS_X64_FUNCTION_POINTER_DECLARATIONS = 1U << 3,
    MINIC_TARGET_LANG_GNU_FIXED_VECTOR_STORAGE = 1U << 4
} MinicTargetLanguageCapability;

/* The source target owns *all* builtin header type names and identities.
 * These descriptions never enable a machine instruction or vector PCS. */
typedef enum MinicTargetBuiltinCoreImportKind {
    MINIC_TARGET_BUILTIN_CORE_IMPORT_NONE = 0,
    MINIC_TARGET_BUILTIN_CORE_IMPORT_OPAQUE,
    MINIC_TARGET_BUILTIN_CORE_IMPORT_VECTOR128
} MinicTargetBuiltinCoreImportKind;

typedef enum MinicTargetBuiltinScalarElementKind {
    MINIC_TARGET_BUILTIN_SCALAR_NONE = 0,
    MINIC_TARGET_BUILTIN_SCALAR_FLOAT32,
    MINIC_TARGET_BUILTIN_SCALAR_FLOAT64,
    MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_CHAR,
    MINIC_TARGET_BUILTIN_SCALAR_SIGNED_CHAR,
    MINIC_TARGET_BUILTIN_SCALAR_UNSIGNED_LONG_LONG
} MinicTargetBuiltinScalarElementKind;

typedef struct MinicTargetSourceBuiltinTypeDescriptor {
    unsigned int id; /* nonzero target-local nominal identity */
    const char *spelling;
    MinicBuiltinDeclarationRole role;
    unsigned int required_capability;
    MinicTargetBuiltinCoreImportKind core_import;
    MinicTargetBuiltinScalarElementKind vector_element;
} MinicTargetSourceBuiltinTypeDescriptor;

typedef enum MinicTargetGnuFunctionAttributeKind {
    MINIC_TARGET_GNU_FUNCTION_NOCF_CHECK = 0,
    MINIC_TARGET_GNU_FUNCTION_INDIRECT_BRANCH,
    MINIC_TARGET_GNU_FUNCTION_TARGET_CPU
} MinicTargetGnuFunctionAttributeKind;

typedef struct MinicTargetGnuPragmaOptionState {
    unsigned int selected_source_features;
} MinicTargetGnuPragmaOptionState;

struct MinicTargetAsmSourcePolicy;
struct MinicTargetMachineBuiltinPolicy;
struct MinicTargetAliasAbiPolicy;
struct MinicTargetSourceLanguagePolicy;
struct MinicTargetVaCursorSourceAbiPolicy;
typedef struct MinicFunction MinicFunction;

typedef struct MinicTargetSourceInfo {
    const struct MinicTargetAsmSourcePolicy *asm_source_policy;
    const struct MinicTargetMachineBuiltinPolicy *machine_builtin_policy;
    const struct MinicTargetAliasAbiPolicy *alias_abi_policy;
    const struct MinicTargetSourceLanguagePolicy *source_language_policy;
    const struct MinicTargetVaCursorSourceAbiPolicy *va_cursor_source_abi_policy;
    const MinicTargetIntegerModel *integer_model;
    /* C scalar storage ranks belong to source semantics, not machine layout. */
    size_t integer_size[MINIC_INTEGER_RANK_INT128 + 1U];
    size_t integer_alignment[MINIC_INTEGER_RANK_INT128 + 1U];
    MinicType wide_character_type;
    MinicType libc_size_type;
    MinicIntegerRank word_integer_rank;
    unsigned int long_double_significand_bits;
    unsigned int language_type_capabilities;
    bool gnu_sizeof_void_is_one;
    bool gnu_sizeof_function_is_one;
} MinicTargetSourceInfo;

/* Per-invocation source overrides copy the immutable source registration.
 * Canonical targets and previous invocation targets are never mutated.
 * Non-copyable after begin(): source_info.integer_model points into this object. */
typedef struct MinicTargetInvocation {
    MinicTargetInfo machine;
    MinicTargetSourceInfo source_info;
    MinicTargetIntegerModel integer_model;
    MinicType default_wide_character_type;
} MinicTargetInvocation;

bool minic_target_invocation_begin(MinicTargetInvocation *invocation,
                                   const MinicTargetInfo *canonical);
bool minic_target_invocation_plain_char(MinicTargetInvocation *invocation,
                                        MinicIntegerSign sign);
bool minic_target_invocation_short_wchar(MinicTargetInvocation *invocation,
                                         bool enabled);

const MinicTargetIntegerModel *minic_target_info_integer_model(
    const MinicTargetInvocation *target);
bool minic_target_info_language_type_supported(
    const MinicTargetInvocation *target, MinicTypeBaseKind kind);
bool minic_target_info_language_type_supported_for_type(
    const MinicTargetInvocation *target, MinicType type);
bool minic_target_info_source_builtin_type_descriptor(
    const MinicTargetInvocation *target, MinicType type,
    MinicTargetSourceBuiltinTypeDescriptor *descriptor);
bool minic_target_info_named_builtin_type(
    const MinicTargetInvocation *target,
    const char *spelling, size_t length, MinicType *type);
bool minic_target_info_header_opaque_vector_return(
    const MinicTargetInvocation *target, MinicType return_type);
bool minic_target_info_machine_builtin_signature(
    const MinicTargetInvocation *target, const char *spelling, size_t length,
    char *callee, size_t callee_capacity, MinicType *result,
    MinicType arguments[3], size_t *argument_count,
    const char **mnemonic, size_t *immediate_index);
bool minic_target_info_header_attribute_supported(
    const MinicTargetInvocation *target, const char *spelling, size_t length);
bool minic_target_info_gnu_function_security_attribute_supported(
    const MinicTargetInvocation *target, const char *attribute, size_t length);
bool minic_target_info_gnu_function_security_attribute_value_supported(
    const MinicTargetInvocation *target, MinicTargetGnuFunctionAttributeKind kind,
    const char *value, size_t value_length);
bool minic_target_info_gnu_pragma_target_options(
    const MinicTargetInvocation *target, const char *value, size_t value_length,
    const MinicTargetGnuPragmaOptionState *current,
    MinicTargetGnuPragmaOptionState *next);
bool minic_target_info_function_pointer_calling_convention_declaration_supported(
    const MinicTargetInvocation *target, MinicFunctionCallingConvention convention);
bool minic_target_info_va_cursor_source_shape(
    const MinicTargetInvocation *target, MinicVaCursorSourceShape *shape);
bool minic_target_info_va_cursor_source_type_supported(
    const MinicTargetInvocation *target, const MinicC0Program *program, MinicType type);
bool minic_target_info_va_cursor_source_operand_supported(
    const MinicTargetInvocation *target, const MinicC0Program *program,
    MinicType type, bool is_lvalue);
/* A scalar cursor assignment copies a C pointer object, so both operands
 * must have compatible types. Structured ABI-state cursors copy their
 * private tag through the source type's existing admission rules. */
bool minic_target_info_va_cursor_source_copy_types_compatible(
    const MinicTargetInvocation *target, const MinicC0Program *program,
    MinicType destination, MinicType source);
bool minic_target_info_plain_char_sign(
    const MinicTargetInvocation *target, MinicIntegerSign *sign);
bool minic_target_info_plain_char_type(const MinicTargetInvocation *target, MinicType *type);
bool minic_target_info_wide_character_type(
    const MinicTargetInvocation *target, MinicType *type);
bool minic_target_info_word_integer_type(
    const MinicTargetInvocation *target, MinicIntegerSign sign, MinicType *type);
bool minic_target_info_function_alias_abi_compatible(
    const MinicTargetInvocation *target, const MinicC0Program *program,
    const MinicFunction *alias, const MinicFunction *owner);
bool minic_target_info_sizeof_type(
    const MinicTargetInvocation *target, const MinicC0Program *program,
    MinicType type, size_t *size);
bool minic_target_info_integer_width(
    const MinicTargetInvocation *target, const MinicC0Program *program,
    MinicType type, unsigned int *bits);
bool minic_target_info_integer_promotion(
    const MinicTargetInvocation *target, MinicType type, MinicType *result);
bool minic_target_info_integer_promotion_for_program(
    const MinicTargetInvocation *target, const MinicC0Program *program,
    MinicType type, MinicType *result);
bool minic_target_info_integer_common(
    const MinicTargetInvocation *target,
    MinicType left, MinicType right, MinicType *result);
bool minic_target_info_integer_common_for_program(
    const MinicTargetInvocation *target, const MinicC0Program *program,
    MinicType left, MinicType right, MinicType *result);
bool minic_target_info_integer_literal_type(
    const MinicTargetInvocation *target, MinicIntegerLiteralBase base,
    bool has_unsigned_suffix, unsigned int long_count,
    uint64_t value, MinicType *result);
bool minic_target_info_inline_asm_readwrite_hardreg_supported(
    const MinicTargetInvocation *, const char *, size_t,
    const char *, size_t, MinicType);
bool minic_target_info_inline_asm_source_clobber_supported(
    const MinicTargetInvocation *target, const char *name, size_t name_length);
bool minic_target_info_inline_asm_register_clobber_supported(
    const MinicTargetInvocation *target, const char *name, size_t name_length);
bool minic_target_info_inline_asm_memory_input_constraint_supported(
    const MinicTargetInvocation *target, const char *constraint, size_t constraint_length);
bool minic_target_info_inline_asm_immediate_may_defer(
    const MinicTargetInvocation *target);
const char *minic_target_info_inline_asm_immediate_constraint_diagnostic(
    const MinicTargetInvocation *target);
bool minic_target_info_inline_asm_immediate_constraint_supported(
    const MinicTargetInvocation *target, const char *constraint,
    size_t constraint_length, int64_t value);

#endif
