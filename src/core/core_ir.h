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
typedef uint32_t MinicCoreFunctionSymbolId;
typedef uint32_t MinicCoreCalleeId;
typedef uint32_t MinicCoreCallSignatureId;
typedef uint32_t MinicCoreInlineAsmId;

#define MINIC_CORE_VALUE_INVALID UINT32_MAX
#define MINIC_CORE_INSTRUCTION_INVALID UINT32_MAX
#define MINIC_CORE_BLOCK_INVALID UINT32_MAX
#define MINIC_CORE_OBJECT_INVALID UINT32_MAX
#define MINIC_CORE_GLOBAL_INVALID UINT32_MAX
#define MINIC_CORE_FUNCTION_SYMBOL_INVALID UINT32_MAX
#define MINIC_CORE_CALLEE_INVALID UINT32_MAX
#define MINIC_CORE_CALL_SIGNATURE_INVALID UINT32_MAX
#define MINIC_CORE_INLINE_ASM_INVALID UINT32_MAX

typedef enum MinicCorePhase { MINIC_CORE_PHASE_EXECUTION_SHADOW = 0 } MinicCorePhase;

typedef enum MinicCoreIntegerOverflowOperator {
    MINIC_CORE_INTEGER_OVERFLOW_ADD = 0,
    MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT,
    MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY
} MinicCoreIntegerOverflowOperator;

/* M79_CALL_FRAME_RETURN_ADDRESS: target-neutral semantic origin for
   GNU call-frame address builtins. Backends may support only a subset of
   kind/level pairs; unsupported pairs remain fail-closed. */
typedef enum MinicCoreCallFrameAddressKind {
    MINIC_CORE_CALL_FRAME_ADDRESS_RETURN = 0,
    MINIC_CORE_CALL_FRAME_ADDRESS_FRAME
} MinicCoreCallFrameAddressKind;

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
    MINIC_CORE_INSTRUCTION_POINTER_LESS,
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
    /* M81_FUNCTION_ADDRESS_VALUE: first-class address of a function symbol. */
    MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS,
    /* M64_LOCAL_LABEL_BLOCK_ADDRESS: target-neutral address of a Core basic block. */
    MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS,
    MINIC_CORE_INSTRUCTION_FIELD_ADDRESS,
    MINIC_CORE_INSTRUCTION_POINTER_OFFSET,
    MINIC_CORE_INSTRUCTION_LOAD,
    MINIC_CORE_INSTRUCTION_STORE,
    /* BATCH_M_RECORD_LOAD: materialize one address-backed aggregate value into
       a private Core object while preserving source volatility. */
    MINIC_CORE_INSTRUCTION_RECORD_LOAD,
    /* M80_ADDRESS_BACKED_RECORD_COPY: byte-preserving aggregate memory copy. */
    MINIC_CORE_INSTRUCTION_RECORD_COPY,
    MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM,
    MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM,
    MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM,
    MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM,
    MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM,
    /* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: target-neutral operand bindings. */
    MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM,
    MINIC_CORE_INSTRUCTION_COMPILER_BARRIER,
    MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS,
    /* M123_VARIADIC_ARGUMENT_ADDRESS: semantic origin of a va_list cursor.
       Backend ABI owns register-save-area placement and the concrete address. */
    MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS,
    MINIC_CORE_INSTRUCTION_CALL,
    /* M83_FIRST_CLASS_INDIRECT_CALL: callee is an SSA function-pointer value. */
    MINIC_CORE_INSTRUCTION_INDIRECT_CALL
} MinicCoreInstructionKind;

/* M91_BUILTIN_UNREACHABLE_TERMINATOR: unreachable is a CFG fact, not a
   value-producing instruction and not an invented target trap. */
typedef enum MinicCoreTerminatorKind {
    MINIC_CORE_TERMINATOR_RETURN = 0,
    MINIC_CORE_TERMINATOR_BRANCH,
    MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH,
    MINIC_CORE_TERMINATOR_UNREACHABLE
} MinicCoreTerminatorKind;

typedef struct MinicCoreValue {
    MinicType type;
    MinicCoreInstructionId definition;
} MinicCoreValue;

typedef struct MinicCoreObject {
    MinicSourceSpan span;
    MinicType type;
    /* M95_REPEATED_LOCAL_OBJECT: legacy frontend arrays carry element type
       plus an explicit count. Keep Core object addressing element-typed while
       owning the complete repeated storage extent. Ordinary objects use 1. */
    size_t element_count;
} MinicCoreObject;

typedef struct MinicCoreGlobal {
    char *name;
    size_t name_length;
    MinicType type;
} MinicCoreGlobal;

typedef struct MinicCoreFunctionSymbol {
    char *name;
    size_t name_length;
} MinicCoreFunctionSymbol;

typedef struct MinicCoreCallee {
    char *name;
    size_t name_length;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
    /* BATCH_D_VARIADIC_DIRECT_CALL: parameter_types is the fixed prefix;
       variadic tail types are the semantic types of VALUE arguments. */
    bool is_variadic;
} MinicCoreCallee;

/* M83_FIRST_CLASS_INDIRECT_CALL: Core owns enough static signature data to
   verify an indirect call without consulting frontend Program state. */
typedef struct MinicCoreCallSignature {
    MinicFunctionTypeId function_type_id;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
} MinicCoreCallSignature;

/* M85_RECORD_CALL_ARGUMENT: Core call arguments carry semantic storage form,
   not target ABI locations. Scalar arguments are SSA values; aggregate
   by-value arguments are immutable snapshots in Core objects. */
typedef enum MinicCoreCallArgumentKind {
    MINIC_CORE_CALL_ARGUMENT_INVALID = 0,
    MINIC_CORE_CALL_ARGUMENT_VALUE,
    MINIC_CORE_CALL_ARGUMENT_OBJECT
} MinicCoreCallArgumentKind;

typedef struct MinicCoreCallArgument {
    MinicCoreCallArgumentKind kind;
    union {
        MinicCoreValueId value_id;
        MinicCoreObjectId object_id;
    } value;
} MinicCoreCallArgument;

typedef struct MinicCoreInlineAsmRegisterClobber {
    char *name;
    size_t name_length;
} MinicCoreInlineAsmRegisterClobber;

typedef struct MinicCoreInlineAsm {
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool has_memory_clobber;
    /* BATCH_L_STRUCTURED_REGISTER_READWRITE: keep register-clobber spelling
       as opaque target metadata. Core does not interpret register names; the
       selected backend only uses them to avoid operand/clobber collisions. */
    MinicCoreInlineAsmRegisterClobber *register_clobbers;
    size_t register_clobber_count;
    size_t register_clobber_capacity;
    /* M76_SINGLE_LABEL_ASM_GOTO: preserve the control-flow target in Core
       instead of hiding it inside target assembly text. The first supported
       seam is one label plus one deferred immediate input. */
    bool is_goto;
    size_t source_inline_asm_id;
    MinicCoreBlockId goto_target;
} MinicCoreInlineAsm;

#define MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT 8U

typedef enum MinicCoreStructuredInlineAsmOperandKind {
    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT = 0,
    /* A register read/write operand is address-backed: load the lvalue before
       asm, bind one target register, then store the post-asm value back. */
    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE,
    /* M107_STRUCTURED_MEMORY_OUTPUT_ASM: address-backed write-only memory
       operand (`=m`). Keep this distinct from read/write memory (`+m`/`+A`). */
    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT,
    /* M125_STRUCTURED_MEMORY_INPUT_ASM: read-only `m` operands carry an
       address into Core, but unlike output/read-write memory they permit const
       pointees and never require post-asm writeback. */
    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT,
    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE,
    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
} MinicCoreStructuredInlineAsmOperandKind;

typedef struct MinicCoreStructuredInlineAsmOperand {
    MinicCoreStructuredInlineAsmOperandKind kind;
    size_t operand_index;
    MinicCoreValueId value;
    /* M105_FIXED_REGISTER_STRUCTURED_ASM: keep frontend-owned local fixed-register
       identity as opaque metadata. Core does not interpret the register spelling;
       the selected backend resolves the binding when materializing asm operands. */
    size_t fixed_register_binding_id;
    bool has_fixed_register_binding;
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
            MinicCoreCallFrameAddressKind kind;
            unsigned int level;
        } call_frame_address;
        struct {
            size_t parameter_index;
            MinicCoreObjectId object_id;
        } parameter_object;
        MinicCoreObjectId object_id;
        MinicCoreGlobalId global_id;
        MinicCoreFunctionSymbolId function_symbol_id;
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
            /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: preserve pointer -=
               as subtraction instead of negating a potentially unsigned index. */
            bool subtract;
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
        struct {
            MinicCoreValueId source_address;
            MinicCoreObjectId destination_object;
            bool is_volatile;
        } record_load;
        struct {
            MinicCoreValueId destination_address;
            MinicCoreValueId source_address;
        } record_copy;
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
            /* M86_DIRECT_RECORD_CALL_RESULT: aggregate call results remain
               address-backed Core objects rather than becoming aggregate SSA. */
            MinicCoreObjectId result_object;
        } call;
        struct {
            MinicCoreValueId callee;
            MinicCoreCallSignatureId signature_id;
            size_t argument_begin;
            size_t argument_count;
        } indirect_call;
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
    MinicCoreFunctionSymbol *function_symbols;
    size_t function_symbol_count;
    size_t function_symbol_capacity;
    MinicCoreCallee *callees;
    size_t callee_count;
    size_t callee_capacity;
    MinicCoreCallSignature *call_signatures;
    size_t call_signature_count;
    size_t call_signature_capacity;
    MinicCoreInlineAsm *inline_asms;
    size_t inline_asm_count;
    size_t inline_asm_capacity;
    MinicCoreCallArgument *call_arguments;
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
bool minic_core_function_add_function_symbol(MinicCoreFunction *function,
                                             const char *name,
                                             size_t name_length,
                                             MinicCoreFunctionSymbolId *symbol_id);
bool minic_core_function_add_object(MinicCoreFunction *function,
                                    MinicSourceSpan span,
                                    MinicType type,
                                    MinicCoreObjectId *object_id);
bool minic_core_function_add_repeated_object(MinicCoreFunction *function,
                                             MinicSourceSpan span,
                                             MinicType element_type,
                                             size_t element_count,
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
                                    bool is_variadic,
                                    MinicCoreCalleeId *callee_id);
bool minic_core_function_add_call_signature(MinicCoreFunction *function,
                                            MinicFunctionTypeId function_type_id,
                                            MinicType return_type,
                                            const MinicType *parameter_types,
                                            size_t parameter_count,
                                            MinicCoreCallSignatureId *signature_id);
bool minic_core_function_add_opaque_inline_asm(MinicCoreFunction *function,
                                               const char *template_text,
                                               size_t template_length,
                                               bool is_volatile,
                                               bool has_memory_clobber,
                                               MinicCoreInlineAsmId *inline_asm_id);
bool minic_core_function_add_inline_asm_register_clobber(
    MinicCoreFunction *function,
    MinicCoreInlineAsmId inline_asm_id,
    const char *name,
    size_t name_length);
bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
                                               const MinicCoreCallArgument *arguments,
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
