#ifndef MINIC_FRONTEND_AST_H
#define MINIC_FRONTEND_AST_H

#include "frontend/token.h"
#include "frontend/type.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef size_t MinicExpressionId;
typedef size_t MinicLocalId;
typedef size_t MinicStatementId;
typedef size_t MinicBlockId;
typedef size_t MinicFunctionId;
typedef size_t MinicTypeAliasId;
typedef size_t MinicEnumeratorId;
typedef size_t MinicGlobalObjectId;
typedef size_t MinicFixedRegisterBindingId;
typedef size_t MinicInlineAsmId;
typedef size_t MinicCleanupContextId;

#define MINIC_EXPRESSION_INVALID ((MinicExpressionId) - 1)
#define MINIC_LOCAL_INVALID ((MinicLocalId) - 1)
#define MINIC_STATEMENT_INVALID ((MinicStatementId) - 1)
#define MINIC_BLOCK_INVALID ((MinicBlockId) - 1)
#define MINIC_FUNCTION_INVALID ((MinicFunctionId) - 1)
#define MINIC_TYPE_ALIAS_INVALID ((MinicTypeAliasId) - 1)
#define MINIC_ENUMERATOR_INVALID ((MinicEnumeratorId) - 1)
#define MINIC_GLOBAL_OBJECT_INVALID ((MinicGlobalObjectId) - 1)
#define MINIC_FIXED_REGISTER_BINDING_INVALID ((MinicFixedRegisterBindingId) - 1)
#define MINIC_INLINE_ASM_INVALID ((MinicInlineAsmId) - 1)
#define MINIC_CLEANUP_CONTEXT_ROOT ((MinicCleanupContextId)0)
#define MINIC_MAX_FUNCTION_PARAMETERS 16U

typedef enum MinicValueCategory { MINIC_VALUE_RVALUE = 0, MINIC_VALUE_LVALUE } MinicValueCategory;

typedef enum MinicExpressionKind {
    MINIC_EXPRESSION_INTEGER = 0,
    MINIC_EXPRESSION_FLOATING,
    MINIC_EXPRESSION_LOCAL,
    MINIC_EXPRESSION_GLOBAL_OBJECT,
    MINIC_EXPRESSION_FIXED_REGISTER,
    MINIC_EXPRESSION_FUNCTION,
    MINIC_EXPRESSION_LABEL_ADDRESS,
    MINIC_EXPRESSION_CALL_FRAME_ADDRESS,
    MINIC_EXPRESSION_BUILTIN_UNREACHABLE,
    MINIC_EXPRESSION_SIZEOF,
    MINIC_EXPRESSION_OFFSETOF,
    MINIC_EXPRESSION_ADDRESS_OF,
    MINIC_EXPRESSION_DEREFERENCE,
    MINIC_EXPRESSION_CAST,
    MINIC_EXPRESSION_BITCAST,
    MINIC_EXPRESSION_CONVERSION,
    MINIC_EXPRESSION_DISCARD,
    MINIC_EXPRESSION_SUBSCRIPT,
    MINIC_EXPRESSION_MEMBER,
    MINIC_EXPRESSION_LVALUE_READ,
    MINIC_EXPRESSION_ASSIGNMENT,
    MINIC_EXPRESSION_COMPOUND_ASSIGNMENT,
    MINIC_EXPRESSION_UNARY,
    MINIC_EXPRESSION_BINARY,
    MINIC_EXPRESSION_CONDITIONAL,
    MINIC_EXPRESSION_CALL,
    MINIC_EXPRESSION_COMPOUND_LITERAL,
    MINIC_EXPRESSION_STATEMENT,
    MINIC_EXPRESSION_BUILTIN_UNARY,
    MINIC_EXPRESSION_BUILTIN_OVERFLOW
} MinicExpressionKind;

typedef enum MinicUnaryOperator {
    MINIC_UNARY_PLUS = 0,
    MINIC_UNARY_NEGATE,
    MINIC_UNARY_LOGICAL_NOT,
    MINIC_UNARY_BITWISE_NOT
} MinicUnaryOperator;

#define MINIC_UNARY_POST_INCREMENT ((MinicUnaryOperator)4)
#define MINIC_UNARY_POST_DECREMENT ((MinicUnaryOperator)5)
#define MINIC_UNARY_PRE_INCREMENT ((MinicUnaryOperator)6)
#define MINIC_UNARY_PRE_DECREMENT ((MinicUnaryOperator)7)

typedef enum MinicBinaryOperator {
    MINIC_BINARY_ADD = 0,
    MINIC_BINARY_SUBTRACT,
    MINIC_BINARY_MULTIPLY,
    MINIC_BINARY_DIVIDE,
    MINIC_BINARY_REMAINDER,
    MINIC_BINARY_SHIFT_LEFT,
    MINIC_BINARY_SHIFT_RIGHT,
    MINIC_BINARY_BITWISE_AND,
    MINIC_BINARY_BITWISE_XOR,
    MINIC_BINARY_BITWISE_OR,
    MINIC_BINARY_EQUAL,
    MINIC_BINARY_NOT_EQUAL,
    MINIC_BINARY_LESS,
    MINIC_BINARY_LESS_EQUAL,
    MINIC_BINARY_GREATER,
    MINIC_BINARY_GREATER_EQUAL,
    MINIC_BINARY_LOGICAL_AND,
    MINIC_BINARY_LOGICAL_OR,
    MINIC_BINARY_COMMA
} MinicBinaryOperator;

typedef enum MinicBuiltinUnaryOperator { MINIC_BUILTIN_UNARY_CLZLL = 0 } MinicBuiltinUnaryOperator;

typedef enum MinicCallFrameAddressKind {
    MINIC_CALL_FRAME_ADDRESS_RETURN = 0,
    MINIC_CALL_FRAME_ADDRESS_FRAME
} MinicCallFrameAddressKind;

typedef enum MinicOverflowOperator {
    MINIC_OVERFLOW_ADD = 0,
    MINIC_OVERFLOW_SUBTRACT,
    MINIC_OVERFLOW_MULTIPLY
} MinicOverflowOperator;

typedef struct MinicExpression {
    MinicExpressionKind kind;
    MinicSourceSpan span;
    MinicType type;
    MinicValueCategory value_category;
    union {
        int64_t integer_value;
        uint64_t floating_bits;
        MinicLocalId local_id;
        MinicGlobalObjectId global_object_id;
        MinicFixedRegisterBindingId fixed_register_binding_id;
        MinicFunctionId function_id;
        MinicStatementId label_statement_id;
        struct {
            MinicCallFrameAddressKind kind;
            unsigned int level;
        } call_frame_address;
        MinicType sizeof_type;
        struct {
            MinicRecordId record_id;
            size_t field_index;
            size_t anonymous_prefix_offset;
        } offsetof_value;
        struct {
            MinicFunctionId function_id;
            MinicExpressionId callee;
            size_t argument_count;
            MinicExpressionId arguments[MINIC_MAX_FUNCTION_PARAMETERS];
        } call;
        struct {
            MinicUnaryOperator operator_kind;
            MinicExpressionId operand;
        } unary;
        struct {
            MinicExpressionId base;
            MinicExpressionId index;
        } subscript;
        struct {
            MinicExpressionId base;
            MinicRecordId record_id;
            size_t field_index;
        } member;
        struct {
            MinicBinaryOperator operator_kind;
            MinicExpressionId left;
            MinicExpressionId right;
        } binary;
        struct {
            MinicExpressionId condition;
            MinicExpressionId when_true;
            MinicExpressionId when_false;
            bool uses_condition_value;
        } conditional;
        struct {
            MinicLocalId local_id;
            MinicBlockId initializer_block;
        } compound_literal;
        struct {
            MinicBlockId block;
            MinicExpressionId result;
        } statement_expression;
        struct {
            MinicBuiltinUnaryOperator operator_kind;
            MinicExpressionId operand;
        } builtin_unary;
        struct {
            MinicOverflowOperator operator_kind;
            MinicExpressionId left;
            MinicExpressionId right;
            MinicExpressionId result_pointer;
        } overflow;
    } value;
} MinicExpression;

typedef struct MinicArrayObjectInfo {
    MinicType element_type;
    size_t element_count;
    bool is_incomplete;
    bool is_zero_length;
    bool has_materialized_type;
} MinicArrayObjectInfo;

typedef struct MinicLocal {
    MinicSourceSpan name_span;
    MinicType type;
    size_t element_count;
    bool is_array;
    bool is_register_storage;
} MinicLocal;

typedef struct MinicCleanupContext {
    MinicCleanupContextId parent;
    MinicExpressionId cleanup_expression;
} MinicCleanupContext;

typedef enum MinicStatementKind {
    MINIC_STATEMENT_ASSIGN = 0,
    MINIC_STATEMENT_RECORD_COPY,
    MINIC_STATEMENT_XOR_ASSIGN,
    MINIC_STATEMENT_EXPRESSION,
    MINIC_STATEMENT_INLINE_ASM,
    MINIC_STATEMENT_RETURN,
    MINIC_STATEMENT_BREAK,
    MINIC_STATEMENT_GOTO,
    MINIC_STATEMENT_LABEL,
    MINIC_STATEMENT_IF,
    MINIC_STATEMENT_WHILE,
    MINIC_STATEMENT_SWITCH,
    MINIC_STATEMENT_CASE,
    MINIC_STATEMENT_DEFAULT
} MinicStatementKind;

typedef struct MinicStatement {
    MinicStatementKind kind;
    MinicSourceSpan span;
    MinicExpressionId target_expression;
    MinicExpressionId expression;
    MinicStatementId target_statement;
    MinicInlineAsmId inline_asm_id;
    MinicCleanupContextId cleanup_context;
    MinicCleanupContextId cleanup_stop_context;
    MinicBlockId then_block;
    MinicBlockId else_block;
} MinicStatement;

typedef enum MinicInlineAsmOperandAccess {
    MINIC_INLINE_ASM_OPERAND_READ_ONLY = 0,
    MINIC_INLINE_ASM_OPERAND_WRITE_ONLY,
    MINIC_INLINE_ASM_OPERAND_READ_WRITE
} MinicInlineAsmOperandAccess;

typedef struct MinicInlineAsmOperand {
    char *name;
    size_t name_length;
    char *constraint_text;
    size_t constraint_length;
    MinicExpressionId expression;
    MinicInlineAsmOperandAccess access;
} MinicInlineAsmOperand;

typedef struct MinicInlineAsmLabel {
    char *name;
    size_t name_length;
    MinicStatementId target_statement;
} MinicInlineAsmLabel;

typedef struct MinicInlineAsmRegisterClobber {
    char *name;
    size_t name_length;
} MinicInlineAsmRegisterClobber;

typedef struct MinicFileAsm {
    char *text;
    size_t length;
} MinicFileAsm;

typedef struct MinicInlineAsm {
    char *template_text;
    size_t template_length;
    MinicInlineAsmOperand *outputs;
    size_t output_count;
    size_t output_capacity;
    MinicInlineAsmOperand *inputs;
    size_t input_count;
    size_t input_capacity;
    MinicInlineAsmLabel *labels;
    size_t label_count;
    size_t label_capacity;
    MinicInlineAsmRegisterClobber *register_clobbers;
    size_t register_clobber_count;
    size_t register_clobber_capacity;
    size_t clobber_count;
    bool is_volatile;
    bool is_goto;
    bool has_memory_clobber;
} MinicInlineAsm;

typedef struct MinicBlock {
    MinicStatementId *statements;
    size_t statement_count;
    size_t statement_capacity;
} MinicBlock;

typedef enum MinicSymbolVisibility {
    MINIC_SYMBOL_VISIBILITY_DEFAULT = 0,
    MINIC_SYMBOL_VISIBILITY_HIDDEN,
    MINIC_SYMBOL_VISIBILITY_INTERNAL,
    MINIC_SYMBOL_VISIBILITY_PROTECTED
} MinicSymbolVisibility;

typedef struct MinicFunction {
    char *name;
    size_t name_length;
    char *assembler_name;
    size_t assembler_name_length;
    char *section_name;
    size_t section_name_length;
    MinicSymbolVisibility visibility;
    MinicType return_type;
    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
    size_t parameter_count;
    size_t local_begin;
    size_t local_count;
    MinicBlockId body_block;
    bool is_defined;
    bool is_internal;
    bool is_variadic;
    bool is_weak;
} MinicFunction;

typedef struct MinicRecordField {
    char *name;
    size_t name_length;
    MinicType type;
    size_t element_count;
    size_t storage_offset;
    size_t bit_width;
    size_t bit_offset;
    size_t explicit_alignment;
    bool is_array;
    bool is_packed;
    bool is_bit_field;
    bool is_flexible_array;
    bool is_zero_length_array;
    bool is_anonymous_member;
} MinicRecordField;

typedef struct MinicRecord {
    char *name;
    size_t name_length;
    MinicRecordField *fields;
    size_t field_count;
    size_t field_capacity;
    size_t storage_size;
    size_t alignment;
    size_t explicit_alignment;
    bool is_union;
    bool is_packed;
    bool is_transparent_union;
    bool is_complete;
} MinicRecord;

typedef struct MinicArrayType {
    MinicType element_type;
    size_t element_count;
    bool is_zero_length;
} MinicArrayType;

typedef struct MinicFunctionType {
    MinicType return_type;
    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
    size_t parameter_count;
} MinicFunctionType;

typedef struct MinicTypeAlias {
    char *name;
    size_t name_length;
    MinicType type;
} MinicTypeAlias;

typedef struct MinicEnum {
    char *name;
    size_t name_length;
    MinicType compatible_type;
    bool is_complete;
} MinicEnum;

typedef struct MinicEnumerator {
    char *name;
    size_t name_length;
    MinicEnumId enum_id;
    MinicType type;
    uint64_t bits;
} MinicEnumerator;

typedef struct MinicFixedRegisterBinding {
    char *name;
    size_t name_length;
    char *register_name;
    size_t register_name_length;
    MinicType type;
} MinicFixedRegisterBinding;

typedef enum MinicGlobalRelocationTargetKind {
    MINIC_GLOBAL_RELOCATION_OBJECT = 0,
    MINIC_GLOBAL_RELOCATION_FUNCTION
} MinicGlobalRelocationTargetKind;

#define MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH 8U

typedef enum MinicGlobalRelocationLocationKind {
    MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR = 0,
    MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT,
    MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD,
    MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR
} MinicGlobalRelocationLocationKind;

typedef struct MinicGlobalRelocation {
    MinicGlobalRelocationLocationKind location_kind;
    size_t location_index;
    MinicGlobalRelocationTargetKind target_kind;
    size_t target_id;
    size_t target_member_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t target_member_depth;
} MinicGlobalRelocation;

typedef struct MinicGlobalObject {
    char *name;
    size_t name_length;
    char *section_name;
    size_t section_name_length;
    MinicType type;
    uint64_t *initializer_values;
    size_t initializer_count;
    size_t initializer_capacity;
    MinicGlobalRelocation *relocations;
    size_t relocation_count;
    size_t relocation_capacity;
    size_t explicit_alignment;
    size_t storage_size;
    size_t alignment;
    MinicSymbolVisibility visibility;
    bool is_internal;
    bool is_read_only;
    bool is_zero_initialized;
    bool is_extern;
    bool is_tentative;
    bool is_block_scope_extern_only;
} MinicGlobalObject;

typedef struct MinicC0Program {
    MinicExpression *expressions;
    size_t expression_count;
    size_t expression_capacity;

    MinicLocal *locals;
    size_t local_count;
    size_t local_capacity;

    MinicCleanupContext *cleanup_contexts;
    size_t cleanup_context_count;
    size_t cleanup_context_capacity;

    MinicStatement *statements;
    size_t statement_count;
    size_t statement_capacity;

    MinicInlineAsm *inline_asms;
    size_t inline_asm_count;
    size_t inline_asm_capacity;

    MinicFileAsm *file_asms;
    size_t file_asm_count;
    size_t file_asm_capacity;

    MinicBlock *blocks;
    size_t block_count;
    size_t block_capacity;
    MinicBlockId body_block;

    MinicFunction *functions;
    size_t function_count;
    size_t function_capacity;
    MinicFunctionId entry_function;

    MinicRecord *records;
    size_t record_count;
    size_t record_capacity;

    MinicArrayType *array_types;
    size_t array_type_count;
    size_t array_type_capacity;

    MinicFunctionType *function_types;
    size_t function_type_count;
    size_t function_type_capacity;

    MinicTypeAlias *type_aliases;
    size_t type_alias_count;
    size_t type_alias_capacity;

    MinicEnum *enums;
    size_t enum_count;
    size_t enum_capacity;

    MinicEnumerator *enumerators;
    size_t enumerator_count;
    size_t enumerator_capacity;

    MinicGlobalObject *global_objects;
    size_t global_object_count;
    size_t global_object_capacity;

    MinicFixedRegisterBinding *fixed_register_bindings;
    size_t fixed_register_binding_count;
    size_t fixed_register_binding_capacity;

    MinicExpressionId return_expression;
} MinicC0Program;

void minic_c0_program_initialize(MinicC0Program *program);
void minic_c0_program_destroy(MinicC0Program *program);

bool minic_c0_program_add_expression(MinicC0Program *program,
                                     const MinicExpression *expression,
                                     MinicExpressionId *expression_id);
bool minic_c0_program_add_local(MinicC0Program *program,
                                const MinicLocal *local,
                                MinicLocalId *local_id);
bool minic_c0_program_add_cleanup_context(MinicC0Program *program,
                                          MinicCleanupContextId parent,
                                          MinicExpressionId cleanup_expression,
                                          MinicCleanupContextId *cleanup_context_id);
bool minic_c0_cleanup_context_reaches(const MinicC0Program *program,
                                      MinicCleanupContextId current,
                                      MinicCleanupContextId stop);
bool minic_c0_program_add_statement(MinicC0Program *program,
                                    const MinicStatement *statement,
                                    MinicStatementId *statement_id);
bool minic_c0_program_add_block(MinicC0Program *program, MinicBlockId *block_id);
bool minic_c0_program_add_file_asm(MinicC0Program *program, const char *text, size_t length);
bool minic_c0_program_add_inline_asm(MinicC0Program *program,
                                     const char *template_text,
                                     size_t template_length,
                                     bool is_volatile,
                                     bool has_memory_clobber,
                                     MinicInlineAsmId *inline_asm_id);
bool minic_c0_program_add_inline_asm_output(MinicC0Program *program,
                                            MinicInlineAsmId inline_asm_id,
                                            const char *name,
                                            size_t name_length,
                                            const char *constraint_text,
                                            size_t constraint_length,
                                            MinicExpressionId expression,
                                            MinicInlineAsmOperandAccess access);
bool minic_c0_program_add_inline_asm_input(MinicC0Program *program,
                                           MinicInlineAsmId inline_asm_id,
                                           const char *name,
                                           size_t name_length,
                                           const char *constraint_text,
                                           size_t constraint_length,
                                           MinicExpressionId expression);
bool minic_c0_program_add_inline_asm_register_clobber(MinicC0Program *program,
                                                      MinicInlineAsmId inline_asm_id,
                                                      const char *name,
                                                      size_t name_length);
bool minic_c0_program_set_inline_asm_memory_clobber(MinicC0Program *program,
                                                    MinicInlineAsmId inline_asm_id,
                                                    bool has_memory_clobber);
bool minic_c0_program_set_inline_asm_goto(MinicC0Program *program,
                                          MinicInlineAsmId inline_asm_id,
                                          bool is_goto);
bool minic_c0_program_add_inline_asm_label(MinicC0Program *program,
                                           MinicInlineAsmId inline_asm_id,
                                           const char *name,
                                           size_t name_length,
                                           MinicStatementId target_statement);
bool minic_c0_block_add_statement(MinicC0Program *program,
                                  MinicBlockId block_id,
                                  MinicStatementId statement_id);
bool minic_c0_program_add_function(MinicC0Program *program,
                                   const char *name,
                                   size_t name_length,
                                   size_t local_begin,
                                   size_t local_count,
                                   MinicBlockId body_block,
                                   MinicFunctionId *function_id);
bool minic_c0_program_set_function_signature(MinicC0Program *program,
                                             MinicFunctionId function_id,
                                             MinicType return_type,
                                             const MinicType *parameter_types,
                                             size_t parameter_count);
bool minic_c0_program_set_function_parameter_count(MinicC0Program *program,
                                                   MinicFunctionId function_id,
                                                   size_t parameter_count);
bool minic_c0_program_set_function_internal(MinicC0Program *program,
                                            MinicFunctionId function_id,
                                            bool is_internal);
bool minic_c0_program_set_function_weak(MinicC0Program *program,
                                        MinicFunctionId function_id,
                                        bool is_weak);
bool minic_c0_program_set_function_assembler_name(MinicC0Program *program,
                                                  MinicFunctionId function_id,
                                                  const char *name,
                                                  size_t name_length);
const char *minic_c0_function_symbol_name(const MinicFunction *function);
bool minic_c0_program_set_function_visibility(MinicC0Program *program,
                                              MinicFunctionId function_id,
                                              MinicSymbolVisibility visibility);
bool minic_c0_program_set_function_section(MinicC0Program *program,
                                           MinicFunctionId function_id,
                                           const char *name,
                                           size_t name_length);
bool minic_c0_program_set_function_variadic(MinicC0Program *program,
                                            MinicFunctionId function_id,
                                            bool is_variadic);
bool minic_c0_program_define_function(MinicC0Program *program,
                                      MinicFunctionId function_id,
                                      size_t local_begin,
                                      MinicBlockId body_block);
bool minic_c0_program_finish_function(MinicC0Program *program,
                                      MinicFunctionId function_id,
                                      size_t local_count);
bool minic_c0_program_add_record(MinicC0Program *program,
                                 const char *name,
                                 size_t name_length,
                                 MinicRecordId *record_id);
bool minic_c0_program_add_anonymous_record(MinicC0Program *program, MinicRecordId *record_id);
bool minic_c0_record_add_field(MinicC0Program *program,
                               MinicRecordId record_id,
                               const char *name,
                               size_t name_length,
                               MinicType type,
                               size_t element_count);
bool minic_c0_record_add_bit_field(MinicC0Program *program,
                                   MinicRecordId record_id,
                                   const char *name,
                                   size_t name_length,
                                   MinicType type,
                                   size_t bit_width);
bool minic_c0_record_add_unnamed_bit_field(MinicC0Program *program,
                                           MinicRecordId record_id,
                                           MinicType type,
                                           size_t bit_width);
bool minic_c0_program_finish_record(MinicC0Program *program, MinicRecordId record_id);
bool minic_c0_program_add_array_type(MinicC0Program *program,
                                     MinicType element_type,
                                     size_t element_count,
                                     MinicType *array_type);
bool minic_c0_program_add_incomplete_array_type(MinicC0Program *program,
                                                MinicType element_type,
                                                MinicType *array_type);
bool minic_c0_program_add_zero_length_array_type(MinicC0Program *program,
                                                 MinicType element_type,
                                                 MinicType *array_type);
bool minic_c0_program_complete_zero_length_array_type(MinicC0Program *program,
                                                      MinicType array_type);
bool minic_c0_program_complete_array_type(MinicC0Program *program,
                                          MinicType array_type,
                                          size_t element_count);
bool minic_c0_program_add_function_type(MinicC0Program *program,
                                        MinicType return_type,
                                        const MinicType *parameter_types,
                                        size_t parameter_count,
                                        MinicType *function_type);
bool minic_c0_program_add_type_alias(MinicC0Program *program,
                                     const char *name,
                                     size_t name_length,
                                     MinicType type,
                                     MinicTypeAliasId *alias_id);
bool minic_c0_program_add_enum(MinicC0Program *program,
                               const char *name,
                               size_t name_length,
                               MinicEnumId *enum_id);
bool minic_c0_program_finish_enum(MinicC0Program *program,
                                  MinicEnumId enum_id,
                                  MinicType compatible_type);
bool minic_c0_program_add_enumerator(MinicC0Program *program,
                                     MinicEnumId enum_id,
                                     const char *name,
                                     size_t name_length,
                                     MinicType type,
                                     uint64_t bits,
                                     MinicEnumeratorId *enumerator_id);
bool minic_c0_types_compatible(const MinicC0Program *program, MinicType left, MinicType right);
bool minic_c0_program_add_fixed_register_binding(MinicC0Program *program,
                                                 const char *name,
                                                 size_t name_length,
                                                 MinicType type,
                                                 const char *register_name,
                                                 size_t register_name_length,
                                                 MinicFixedRegisterBindingId *binding_id);
bool minic_c0_program_add_global_object(MinicC0Program *program,
                                        const char *name,
                                        size_t name_length,
                                        MinicType type,
                                        bool is_internal,
                                        bool is_read_only,
                                        MinicGlobalObjectId *global_object_id);
bool minic_c0_program_add_extern_global_object(MinicC0Program *program,
                                               const char *name,
                                               size_t name_length,
                                               MinicType type,
                                               bool is_read_only,
                                               MinicGlobalObjectId *global_object_id);
bool minic_c0_program_add_tentative_global_object(MinicC0Program *program,
                                                  const char *name,
                                                  size_t name_length,
                                                  MinicType type,
                                                  bool is_internal,
                                                  bool is_read_only,
                                                  MinicGlobalObjectId *global_object_id);
bool minic_c0_global_object_merge_tentative(MinicC0Program *program,
                                            MinicGlobalObjectId global_object_id);
bool minic_c0_global_object_begin_definition(MinicC0Program *program,
                                             MinicGlobalObjectId global_object_id);
bool minic_c0_global_object_add_initializer(MinicC0Program *program,
                                            MinicGlobalObjectId global_object_id,
                                            int value);
bool minic_c0_global_object_add_initializer_bits(MinicC0Program *program,
                                                 MinicGlobalObjectId global_object_id,
                                                 uint64_t bits);
bool minic_c0_global_object_add_function_relocation(MinicC0Program *program,
                                                    MinicGlobalObjectId global_object_id,
                                                    MinicGlobalRelocationLocationKind location_kind,
                                                    size_t location_index,
                                                    MinicFunctionId function_id);
bool minic_c0_global_object_add_object_relocation(MinicC0Program *program,
                                                  MinicGlobalObjectId global_object_id,
                                                  MinicGlobalRelocationLocationKind location_kind,
                                                  size_t location_index,
                                                  MinicGlobalObjectId target_object_id);
bool minic_c0_global_object_add_object_relocation_path(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth);
bool minic_c0_global_relocation_slot_type(const MinicC0Program *program,
                                          const MinicGlobalObject *object,
                                          MinicGlobalRelocationLocationKind location_kind,
                                          size_t location_index,
                                          MinicType *slot_type);
bool minic_c0_global_relocation_object_target_type(const MinicC0Program *program,
                                                   const MinicGlobalRelocation *relocation,
                                                   MinicType *target_type);
bool minic_c0_global_relocation_object_target_compatible(const MinicC0Program *program,
                                                         const MinicGlobalRelocation *relocation,
                                                         MinicType slot_type);
bool minic_c0_global_object_set_zero_initialized(MinicC0Program *program,
                                                 MinicGlobalObjectId global_object_id);
bool minic_c0_global_object_set_extern(MinicC0Program *program,
                                       MinicGlobalObjectId global_object_id);
bool minic_c0_global_object_set_visibility(MinicC0Program *program,
                                           MinicGlobalObjectId global_object_id,
                                           MinicSymbolVisibility visibility);
bool minic_c0_global_object_set_section(MinicC0Program *program,
                                        MinicGlobalObjectId global_object_id,
                                        const char *name,
                                        size_t name_length);
bool minic_c0_global_object_set_explicit_alignment(MinicC0Program *program,
                                                   MinicGlobalObjectId global_object_id,
                                                   size_t alignment);

/* Program entity accessors return borrowed pointers into growable owner arrays.
 * IDs remain stable, but growing the same entity array may relocate its storage.
 * Keep an ID or copy required value fields across any operation that may grow that pool. */
const MinicExpression *minic_c0_program_expression(const MinicC0Program *program,
                                                   MinicExpressionId expression_id);
bool minic_c0_expression_array_object_info(const MinicC0Program *program,
                                           const MinicExpression *expression,
                                           MinicArrayObjectInfo *info);
const MinicRecordField *minic_c0_expression_bit_field(const MinicC0Program *program,
                                                      MinicExpressionId expression_id);
bool minic_c0_record_value_is_address_backed(const MinicC0Program *program,
                                             MinicExpressionId expression_id);
bool minic_c0_record_value_is_copy_source(const MinicC0Program *program,
                                          MinicExpressionId expression_id);
bool minic_c0_expression_is_null_pointer_constant_v0(const MinicC0Program *program,
                                                     MinicExpressionId expression_id);
bool minic_c0_conditional_result_type(const MinicC0Program *program,
                                      MinicExpressionId when_true_expression_id,
                                      MinicExpressionId when_false_expression_id,
                                      MinicType *result);
bool minic_c0_assignment_compatible(const MinicC0Program *program,
                                    MinicType target_type,
                                    MinicExpressionId source_expression_id);
bool minic_c0_fixed_call_argument_compatible(const MinicC0Program *program,
                                             MinicType parameter_type,
                                             MinicExpressionId argument_expression_id);
bool minic_c0_fixed_parameter_abi_type(const MinicC0Program *program,
                                       MinicType parameter_type,
                                       MinicType *abi_type);
bool minic_c0_pointer_equality_compatible(const MinicC0Program *program,
                                          MinicExpressionId left_expression_id,
                                          MinicExpressionId right_expression_id);
bool minic_c0_type_is_complete_object(const MinicC0Program *program, MinicType type);
bool minic_c0_pointer_arithmetic_pointee_allowed(const MinicC0Program *program,
                                                 MinicType pointee_type);
bool minic_c0_pointer_relational_compatible(const MinicC0Program *program,
                                            MinicType left,
                                            MinicType right);
const MinicLocal *minic_c0_program_local(const MinicC0Program *program, MinicLocalId local_id);
const MinicCleanupContext *
minic_c0_program_cleanup_context(const MinicC0Program *program,
                                 MinicCleanupContextId cleanup_context_id);
const MinicStatement *minic_c0_program_statement(const MinicC0Program *program,
                                                 MinicStatementId statement_id);
const MinicBlock *minic_c0_program_block(const MinicC0Program *program, MinicBlockId block_id);
const MinicInlineAsm *minic_c0_program_inline_asm(const MinicC0Program *program,
                                                  MinicInlineAsmId inline_asm_id);
const MinicFunction *minic_c0_program_function(const MinicC0Program *program,
                                               MinicFunctionId function_id);
const MinicRecord *minic_c0_program_record(const MinicC0Program *program, MinicRecordId record_id);
const MinicRecordField *minic_c0_record_field(const MinicRecord *record, size_t field_index);
const MinicArrayType *minic_c0_program_array_type(const MinicC0Program *program,
                                                  MinicArrayTypeId array_type_id);
const MinicFunctionType *minic_c0_program_function_type(const MinicC0Program *program,
                                                        MinicFunctionTypeId function_type_id);
const MinicTypeAlias *minic_c0_program_type_alias(const MinicC0Program *program,
                                                  MinicTypeAliasId alias_id);
const MinicEnum *minic_c0_program_enum(const MinicC0Program *program, MinicEnumId enum_id);
const MinicEnumerator *minic_c0_program_enumerator(const MinicC0Program *program,
                                                   MinicEnumeratorId enumerator_id);
const MinicGlobalObject *minic_c0_program_global_object(const MinicC0Program *program,
                                                        MinicGlobalObjectId global_object_id);
const MinicFixedRegisterBinding *
minic_c0_program_fixed_register_binding(const MinicC0Program *program,
                                        MinicFixedRegisterBindingId binding_id);

#endif
