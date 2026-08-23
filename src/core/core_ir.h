#ifndef MINIC_CORE_CORE_IR_H
#define MINIC_CORE_CORE_IR_H

#include "frontend/token.h"
#include "frontend/type.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef uint32_t MinicCoreValueId;
typedef uint32_t MinicCoreInstructionId;
typedef uint32_t MinicCoreBlockId;
typedef uint32_t MinicCoreObjectId;
typedef uint32_t MinicCoreGlobalId;
typedef uint32_t MinicCoreCalleeId;
typedef uint32_t MinicCoreInlineAsmId;

#define MINIC_CORE_VALUE_INVALID UINT32_MAX
#define MINIC_CORE_INSTRUCTION_INVALID UINT32_MAX
#define MINIC_CORE_BLOCK_INVALID UINT32_MAX
#define MINIC_CORE_OBJECT_INVALID UINT32_MAX
#define MINIC_CORE_GLOBAL_INVALID UINT32_MAX
#define MINIC_CORE_CALLEE_INVALID UINT32_MAX
#define MINIC_CORE_INLINE_ASM_INVALID UINT32_MAX

typedef enum MinicCorePhase { MINIC_CORE_PHASE_EXECUTION_SHADOW = 0 } MinicCorePhase;

typedef enum MinicCoreIntegerOverflowOperator {
    MINIC_CORE_INTEGER_OVERFLOW_ADD = 0,
    MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT,
    MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY
} MinicCoreIntegerOverflowOperator;

typedef enum MinicCoreInstructionKind {
    MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT = 0,
    MINIC_CORE_INSTRUCTION_INTEGER_ADD,
    MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT,
    MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY,
    MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE,
    MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER,
    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND,
    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR,
    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR,
    MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT,
    MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT,
    MINIC_CORE_INSTRUCTION_INTEGER_LESS,
    MINIC_CORE_INSTRUCTION_SCALAR_EQUAL,
    MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW,
    MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION,
    MINIC_CORE_INSTRUCTION_SCALAR_BITCAST,
    MINIC_CORE_INSTRUCTION_INTEGER_NEGATE,
    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT,
    MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO,
    MINIC_CORE_INSTRUCTION_PARAMETER,
    MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ,
    MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT,
    MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS,
    MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS,
    /* M64_LOCAL_LABEL_BLOCK_ADDRESS: target-neutral address of a Core basic block. */
    MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS,
    MINIC_CORE_INSTRUCTION_FIELD_ADDRESS,
    MINIC_CORE_INSTRUCTION_POINTER_OFFSET,
    MINIC_CORE_INSTRUCTION_LOAD,
    MINIC_CORE_INSTRUCTION_STORE,
    MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM,
    MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM,
    MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM,
    MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM,
    MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM,
    /* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: target-neutral operand bindings. */
    MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM,
    MINIC_CORE_INSTRUCTION_COMPILER_BARRIER,
    MINIC_CORE_INSTRUCTION_CALL
} MinicCoreInstructionKind;

typedef enum MinicCoreTerminatorKind {
    MINIC_CORE_TERMINATOR_RETURN = 0,
    MINIC_CORE_TERMINATOR_BRANCH,
    MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH
} MinicCoreTerminatorKind;

typedef struct MinicCoreValue {
    MinicType type;
    MinicCoreInstructionId definition;
} MinicCoreValue;

typedef struct MinicCoreObject {
    MinicSourceSpan span;
    MinicType type;
} MinicCoreObject;

typedef struct MinicCoreGlobal {
    char *name;
    size_t name_length;
    MinicType type;
} MinicCoreGlobal;

typedef struct MinicCoreCallee {
    char *name;
    size_t name_length;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
} MinicCoreCallee;

typedef struct MinicCoreInlineAsm {
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool has_memory_clobber;
} MinicCoreInlineAsm;

#define MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT 8U

typedef enum MinicCoreStructuredInlineAsmOperandKind {
    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT = 0,
    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE,
    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
} MinicCoreStructuredInlineAsmOperandKind;

typedef struct MinicCoreStructuredInlineAsmOperand {
    MinicCoreStructuredInlineAsmOperandKind kind;
    size_t operand_index;
    MinicCoreValueId value;
} MinicCoreStructuredInlineAsmOperand;

typedef struct MinicCoreInstruction {
    MinicCoreInstructionKind kind;
    MinicSourceSpan span;
    MinicType type;
    MinicCoreValueId result;
    union {
        int64_t integer_value;
        struct {
            MinicCoreValueId left;
            MinicCoreValueId right;
        } binary;
        struct {
            MinicCoreIntegerOverflowOperator operator_kind;
            MinicCoreValueId left;
            MinicCoreValueId right;
            MinicCoreValueId result_address;
        } integer_overflow;
        MinicCoreValueId operand;
        size_t parameter_index;
        size_t fixed_register_binding_id;
        struct {
            size_t parameter_index;
            MinicCoreObjectId object_id;
        } parameter_object;
        MinicCoreObjectId object_id;
        MinicCoreGlobalId global_id;
        MinicCoreBlockId block_id;
        struct {
            MinicCoreValueId base;
            MinicRecordId record_id;
            size_t field_index;
        } field_address;
        struct {
            MinicCoreValueId base;
            MinicCoreValueId index;
            size_t element_size;
        } pointer_offset;
        struct {
            MinicCoreValueId address;
            bool is_volatile;
        } load;
        struct {
            MinicCoreValueId address;
            MinicCoreValueId stored_value;
            bool is_volatile;
        } store;
        MinicCoreInlineAsmId inline_asm_id;
        struct {
            MinicCoreInlineAsmId inline_asm_id;
            MinicCoreValueId operand;
        } register_output_input_inline_asm;
        struct {
            MinicCoreInlineAsmId inline_asm_id;
            MinicCoreValueId memory_address;
            MinicCoreValueId operand;
            size_t memory_operand_index;
            size_t register_output_operand_index;
            size_t scalar_input_operand_index;
        } memory_readwrite_scalar_input_inline_asm;
        struct {
            MinicCoreInlineAsmId inline_asm_id;
            MinicCoreValueId operand;
        } scalar_input_inline_asm;
        struct {
            MinicCoreInlineAsmId inline_asm_id;
            size_t operand_count;
            MinicCoreStructuredInlineAsmOperand operands[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
        } structured_inline_asm;
        struct {
            MinicCoreCalleeId callee_id;
            size_t argument_begin;
            size_t argument_count;
        } call;
    } value;
} MinicCoreInstruction;

typedef struct MinicCoreTerminator {
    MinicCoreTerminatorKind kind;
    MinicSourceSpan span;
    MinicCoreValueId return_value;
    MinicCoreObjectId return_object;
    MinicCoreBlockId branch_target;
    struct {
        MinicCoreValueId condition;
        MinicCoreBlockId when_true;
        MinicCoreBlockId when_false;
    } conditional;
} MinicCoreTerminator;

typedef struct MinicCoreBlock {
    MinicCoreInstructionId *instructions;
    size_t instruction_count;
    size_t instruction_capacity;
    MinicCoreTerminator terminator;
    bool has_terminator;
} MinicCoreBlock;

typedef struct MinicCoreFunction {
    MinicCorePhase phase;
    char *name;
    size_t name_length;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
    MinicCoreGlobal *globals;
    size_t global_count;
    size_t global_capacity;
    MinicCoreCallee *callees;
    size_t callee_count;
    size_t callee_capacity;
    MinicCoreInlineAsm *inline_asms;
    size_t inline_asm_count;
    size_t inline_asm_capacity;
    MinicCoreValueId *call_arguments;
    size_t call_argument_count;
    size_t call_argument_capacity;
    MinicCoreObject *objects;
    size_t object_count;
    size_t object_capacity;
    MinicCoreValue *values;
    size_t value_count;
    size_t value_capacity;
    MinicCoreInstruction *instructions;
    size_t instruction_count;
    size_t instruction_capacity;
    MinicCoreBlock *blocks;
    size_t block_count;
    size_t block_capacity;
    MinicCoreBlockId entry_block;
} MinicCoreFunction;

bool minic_core_scalar_bitcast_types_valid(MinicType target_type, MinicType source_type);
void minic_core_function_initialize(MinicCoreFunction *function);
void minic_core_function_destroy(MinicCoreFunction *function);
bool minic_core_function_set_signature(MinicCoreFunction *function,
                                       const char *name,
                                       size_t name_length,
                                       MinicType return_type,
                                       const MinicType *parameter_types,
                                       size_t parameter_count);
bool minic_core_function_add_block(MinicCoreFunction *function, MinicCoreBlockId *block_id);
bool minic_core_function_add_object(MinicCoreFunction *function,
                                    MinicSourceSpan span,
                                    MinicType type,
                                    MinicCoreObjectId *object_id);
bool minic_core_function_add_global(MinicCoreFunction *function,
                                    const char *name,
                                    size_t name_length,
                                    MinicType type,
                                    MinicCoreGlobalId *global_id);
bool minic_core_function_add_callee(MinicCoreFunction *function,
                                    const char *name,
                                    size_t name_length,
                                    MinicType return_type,
                                    const MinicType *parameter_types,
                                    size_t parameter_count,
                                    MinicCoreCalleeId *callee_id);
bool minic_core_function_add_opaque_inline_asm(MinicCoreFunction *function,
                                               const char *template_text,
                                               size_t template_length,
                                               bool is_volatile,
                                               bool has_memory_clobber,
                                               MinicCoreInlineAsmId *inline_asm_id);
bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
                                               const MinicCoreValueId *arguments,
                                               size_t argument_count,
                                               size_t *argument_begin);
bool minic_core_function_append_value_instruction(MinicCoreFunction *function,
                                                  MinicCoreBlockId block_id,
                                                  const MinicCoreInstruction *instruction,
                                                  MinicCoreValueId *value_id);
bool minic_core_function_append_effect_instruction(MinicCoreFunction *function,
                                                   MinicCoreBlockId block_id,
                                                   const MinicCoreInstruction *instruction);
bool minic_core_function_set_terminator(MinicCoreFunction *function,
                                        MinicCoreBlockId block_id,
                                        const MinicCoreTerminator *terminator);
bool minic_core_function_verify(const MinicCoreFunction *function);
bool minic_core_function_dump(FILE *output, const MinicCoreFunction *function);

#endif
