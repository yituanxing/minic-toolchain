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

#define MINIC_CORE_VALUE_INVALID UINT32_MAX
#define MINIC_CORE_INSTRUCTION_INVALID UINT32_MAX
#define MINIC_CORE_BLOCK_INVALID UINT32_MAX
#define MINIC_CORE_OBJECT_INVALID UINT32_MAX

typedef enum MinicCorePhase { MINIC_CORE_PHASE_EXECUTION_SHADOW = 0 } MinicCorePhase;

typedef enum MinicCoreInstructionKind {
    MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT = 0,
    MINIC_CORE_INSTRUCTION_INTEGER_ADD,
    MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS,
    MINIC_CORE_INSTRUCTION_LOAD,
    MINIC_CORE_INSTRUCTION_STORE
} MinicCoreInstructionKind;

typedef enum MinicCoreTerminatorKind { MINIC_CORE_TERMINATOR_RETURN = 0 } MinicCoreTerminatorKind;

typedef struct MinicCoreValue {
    MinicType type;
    MinicCoreInstructionId definition;
} MinicCoreValue;

typedef struct MinicCoreObject {
    MinicSourceSpan span;
    MinicType type;
} MinicCoreObject;

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
        MinicCoreObjectId object_id;
        struct {
            MinicCoreValueId address;
            bool is_volatile;
        } load;
        struct {
            MinicCoreValueId address;
            MinicCoreValueId stored_value;
            bool is_volatile;
        } store;
    } value;
} MinicCoreInstruction;

typedef struct MinicCoreTerminator {
    MinicCoreTerminatorKind kind;
    MinicSourceSpan span;
    MinicCoreValueId return_value;
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
