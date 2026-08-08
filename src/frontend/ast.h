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
typedef size_t MinicGlobalObjectId;

#define MINIC_EXPRESSION_INVALID ((MinicExpressionId) - 1)
#define MINIC_LOCAL_INVALID ((MinicLocalId) - 1)
#define MINIC_STATEMENT_INVALID ((MinicStatementId) - 1)
#define MINIC_BLOCK_INVALID ((MinicBlockId) - 1)
#define MINIC_FUNCTION_INVALID ((MinicFunctionId) - 1)
#define MINIC_TYPE_ALIAS_INVALID ((MinicTypeAliasId) - 1)
#define MINIC_GLOBAL_OBJECT_INVALID ((MinicGlobalObjectId) - 1)

typedef enum MinicValueCategory { MINIC_VALUE_RVALUE = 0, MINIC_VALUE_LVALUE } MinicValueCategory;

typedef enum MinicExpressionKind {
    MINIC_EXPRESSION_INTEGER = 0,
    MINIC_EXPRESSION_FLOATING,
    MINIC_EXPRESSION_LOCAL,
    MINIC_EXPRESSION_GLOBAL_OBJECT,
    MINIC_EXPRESSION_FUNCTION,
    MINIC_EXPRESSION_SIZEOF,
    MINIC_EXPRESSION_ADDRESS_OF,
    MINIC_EXPRESSION_DEREFERENCE,
    MINIC_EXPRESSION_CAST,
    MINIC_EXPRESSION_BITCAST,
    MINIC_EXPRESSION_CONVERSION,
    MINIC_EXPRESSION_SUBSCRIPT,
    MINIC_EXPRESSION_MEMBER,
    MINIC_EXPRESSION_UNARY,
    MINIC_EXPRESSION_BINARY,
    MINIC_EXPRESSION_CALL
} MinicExpressionKind;

typedef enum MinicUnaryOperator {
    MINIC_UNARY_PLUS = 0,
    MINIC_UNARY_NEGATE,
    MINIC_UNARY_LOGICAL_NOT
} MinicUnaryOperator;

#define MINIC_UNARY_POST_INCREMENT ((MinicUnaryOperator)3)
#define MINIC_UNARY_POST_DECREMENT ((MinicUnaryOperator)4)

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
    MINIC_BINARY_EQUAL,
    MINIC_BINARY_NOT_EQUAL,
    MINIC_BINARY_LESS,
    MINIC_BINARY_LESS_EQUAL,
    MINIC_BINARY_GREATER,
    MINIC_BINARY_GREATER_EQUAL,
    MINIC_BINARY_LOGICAL_AND,
    MINIC_BINARY_LOGICAL_OR
} MinicBinaryOperator;

typedef struct MinicExpression {
    MinicExpressionKind kind;
    MinicSourceSpan span;
    MinicType type;
    MinicValueCategory value_category;
    union {
        int integer_value;
        uint64_t floating_bits;
        MinicLocalId local_id;
        MinicGlobalObjectId global_object_id;
        MinicFunctionId function_id;
        MinicType sizeof_type;
        struct {
            MinicFunctionId function_id;
            MinicExpressionId callee;
            size_t argument_count;
            MinicExpressionId arguments[8];
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
    } value;
} MinicExpression;

typedef struct MinicLocal {
    MinicSourceSpan name_span;
    MinicType type;
    size_t element_count;
    size_t storage_offset;
} MinicLocal;

typedef enum MinicStatementKind {
    MINIC_STATEMENT_ASSIGN = 0,
    MINIC_STATEMENT_XOR_ASSIGN,
    MINIC_STATEMENT_EXPRESSION,
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
    MinicBlockId then_block;
    MinicBlockId else_block;
} MinicStatement;

typedef struct MinicBlock {
    MinicStatementId *statements;
    size_t statement_count;
    size_t statement_capacity;
} MinicBlock;

typedef struct MinicFunction {
    char *name;
    size_t name_length;
    MinicType return_type;
    MinicType parameter_types[8];
    size_t parameter_count;
    size_t local_begin;
    size_t local_count;
    size_t local_storage_size;
    MinicBlockId body_block;
    bool is_defined;
    bool is_internal;
    bool is_variadic;
} MinicFunction;

typedef struct MinicRecordField {
    char *name;
    size_t name_length;
    MinicType type;
    size_t element_count;
    size_t storage_offset;
} MinicRecordField;

typedef struct MinicRecord {
    char *name;
    size_t name_length;
    MinicRecordField *fields;
    size_t field_count;
    size_t field_capacity;
    size_t storage_size;
    size_t alignment;
    bool is_complete;
} MinicRecord;

typedef struct MinicArrayType {
    MinicType element_type;
    size_t element_count;
} MinicArrayType;

typedef struct MinicFunctionType {
    MinicType return_type;
    MinicType parameter_types[8];
    size_t parameter_count;
} MinicFunctionType;

typedef struct MinicTypeAlias {
    char *name;
    size_t name_length;
    MinicType type;
} MinicTypeAlias;

typedef struct MinicGlobalFunctionRelocation {
    size_t field_index;
    MinicFunctionId function_id;
} MinicGlobalFunctionRelocation;

typedef struct MinicGlobalObject {
    char *name;
    size_t name_length;
    MinicType type;
    int *initializer_values;
    size_t initializer_count;
    size_t initializer_capacity;
    MinicGlobalFunctionRelocation function_relocations[8];
    size_t function_relocation_count;
    size_t storage_size;
    size_t alignment;
    bool is_internal;
    bool is_read_only;
    bool is_zero_initialized;
} MinicGlobalObject;

typedef struct MinicC0Program {
    MinicExpression *expressions;
    size_t expression_count;
    size_t expression_capacity;

    MinicLocal *locals;
    size_t local_count;
    size_t local_capacity;

    MinicStatement *statements;
    size_t statement_count;
    size_t statement_capacity;

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

    MinicGlobalObject *global_objects;
    size_t global_object_count;
    size_t global_object_capacity;

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
bool minic_c0_program_add_statement(MinicC0Program *program,
                                    const MinicStatement *statement,
                                    MinicStatementId *statement_id);
bool minic_c0_program_add_block(MinicC0Program *program, MinicBlockId *block_id);
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
bool minic_c0_program_finish_record(MinicC0Program *program, MinicRecordId record_id);
bool minic_c0_program_add_array_type(MinicC0Program *program,
                                     MinicType element_type,
                                     size_t element_count,
                                     MinicType *array_type);
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
bool minic_c0_program_add_global_object(MinicC0Program *program,
                                        const char *name,
                                        size_t name_length,
                                        MinicType type,
                                        bool is_internal,
                                        bool is_read_only,
                                        MinicGlobalObjectId *global_object_id);
bool minic_c0_global_object_add_initializer(MinicC0Program *program,
                                            MinicGlobalObjectId global_object_id,
                                            int value);
bool minic_c0_global_object_add_function_relocation(MinicC0Program *program,
                                                    MinicGlobalObjectId global_object_id,
                                                    size_t field_index,
                                                    MinicFunctionId function_id);
bool minic_c0_global_object_set_zero_initialized(MinicC0Program *program,
                                                 MinicGlobalObjectId global_object_id);

const MinicExpression *minic_c0_program_expression(const MinicC0Program *program,
                                                   MinicExpressionId expression_id);
bool minic_c0_assignment_compatible(const MinicC0Program *program,
                                    MinicType target_type,
                                    MinicExpressionId source_expression_id);
bool minic_c0_pointer_equality_compatible(const MinicC0Program *program,
                                          MinicExpressionId left_expression_id,
                                          MinicExpressionId right_expression_id);
const MinicLocal *minic_c0_program_local(const MinicC0Program *program, MinicLocalId local_id);
const MinicStatement *minic_c0_program_statement(const MinicC0Program *program,
                                                 MinicStatementId statement_id);
const MinicBlock *minic_c0_program_block(const MinicC0Program *program, MinicBlockId block_id);
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
const MinicGlobalObject *minic_c0_program_global_object(const MinicC0Program *program,
                                                        MinicGlobalObjectId global_object_id);

#endif
