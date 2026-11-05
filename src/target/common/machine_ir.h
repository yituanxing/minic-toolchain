#ifndef MINIC_TARGET_COMMON_MACHINE_IR_H
#define MINIC_TARGET_COMMON_MACHINE_IR_H

#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A physical, target-scoped instruction model. This is intentionally not a
 * universal opcode table, virtual-register IR, or an assembler-line buffer.
 * Target selection owns opcode numbers and the independent opcode contract.
 * A MachineFunction owns all strings, blocks, machine operands and frame
 * references after lowering returns. */
#define MINIC_MACHINE_MAX_OPERANDS 16U
#define MINIC_MACHINE_MAX_REGISTER_EFFECTS 64U

typedef enum MinicMachineOperandKind {
    MINIC_MACHINE_OPERAND_INVALID = 0,
    MINIC_MACHINE_OPERAND_REGISTER,
    MINIC_MACHINE_OPERAND_INTEGER,
    MINIC_MACHINE_OPERAND_FRAME,
    MINIC_MACHINE_OPERAND_SYMBOL,
    MINIC_MACHINE_OPERAND_BLOCK,
    MINIC_MACHINE_OPERAND_ADDRESS,
    /* Machine-local label materialization, e.g. la rd,.Lcomputed_goto.
     * This is not a taken branch and creates no CFG successor. */
    MINIC_MACHINE_OPERAND_BLOCK_ADDRESS
} MinicMachineOperandKind;

typedef enum MinicMachineRegisterBank {
    MINIC_MACHINE_REGISTER_INVALID = 0,
    MINIC_MACHINE_REGISTER_GENERAL,
    MINIC_MACHINE_REGISTER_FLOAT,
    MINIC_MACHINE_REGISTER_VECTOR,
    MINIC_MACHINE_REGISTER_FLAGS,
    MINIC_MACHINE_REGISTER_SYSTEM
} MinicMachineRegisterBank;

typedef enum MinicMachineRegisterAccess {
    MINIC_MACHINE_REGISTER_READ = 1U << 0,
    MINIC_MACHINE_REGISTER_WRITE = 1U << 1,
    MINIC_MACHINE_REGISTER_CLOBBER = 1U << 2
} MinicMachineRegisterAccess;

typedef struct MinicMachineRegister {
    uint32_t number;
    uint16_t width_bits;
    MinicMachineRegisterBank bank;
} MinicMachineRegister;

typedef struct MinicMachineRegisterEffect {
    MinicMachineRegister reg;
    unsigned int access;
} MinicMachineRegisterEffect;

typedef enum MinicMachineFrameArea {
    MINIC_MACHINE_FRAME_LOCAL = 0,
    MINIC_MACHINE_FRAME_INCOMING,
    MINIC_MACHINE_FRAME_OUTGOING
} MinicMachineFrameArea;

/* Fixed offsets are interpreted against an explicit machine frame base;
 * dynamic alloca invalidates stable SP-relative local/incoming references. */
typedef enum MinicMachineFrameBase {
    MINIC_MACHINE_FRAME_BASE_STACK_POINTER = 0,
    MINIC_MACHINE_FRAME_BASE_FRAME_POINTER,
    MINIC_MACHINE_FRAME_BASE_ENTRY_STACK_POINTER
} MinicMachineFrameBase;

typedef struct MinicMachineFrameObject {
    size_t size_bytes;
    size_t alignment_bytes;
    MinicMachineFrameArea area;
    MinicMachineFrameBase base;
    bool has_fixed_offset;
    int64_t fixed_offset;
} MinicMachineFrameObject;

/* Address syntax/encoding is target-owned. The tuple describes hardware
 * register identities and arithmetic only; validation of mode and scale is
 * mandatory through a target policy. */
typedef struct MinicMachineAddress {
    uint32_t target_mode;
    bool has_base;
    bool has_index;
    bool has_segment;
    MinicMachineRegister base;
    MinicMachineRegister index;
    MinicMachineRegister segment;
    unsigned int scale;
    int64_t displacement;
    /* A relocation can be one component of a machine addressing mode,
     * e.g. x86 GOTPCREL(symbol)(%rip). Owned after append. */
    bool has_symbol;
    char *symbol;
    uint32_t relocation;
    int64_t addend;
} MinicMachineAddress;

typedef struct MinicMachineOperand {
    MinicMachineOperandKind kind;
    union {
        struct {
            MinicMachineRegister reg;
            unsigned int access;
        } physical;
        int64_t integer;
        struct {
            size_t index;
            int64_t displacement;
        } frame;
        struct {
            char *name;
            uint32_t relocation;
            int64_t addend;
        } symbol;
        size_t block_id;
        MinicMachineAddress address;
    } value;
    /* Set only for a source-owned inline-ASM operand, never for generated
     * instructions. This is an owned C string, not parsed machine syntax. */
    char *asm_constraint;
} MinicMachineOperand;

typedef enum MinicMachineEffects {
    MINIC_MACHINE_EFFECT_MEMORY_READ = 1U << 0,
    MINIC_MACHINE_EFFECT_MEMORY_WRITE = 1U << 1,
    MINIC_MACHINE_EFFECT_VOLATILE = 1U << 2,
    MINIC_MACHINE_EFFECT_COMPILER_BARRIER = 1U << 3,
    MINIC_MACHINE_EFFECT_CALL = 1U << 4,
    MINIC_MACHINE_EFFECT_BRANCH = 1U << 5,
    MINIC_MACHINE_EFFECT_RETURN = 1U << 6,
    MINIC_MACHINE_EFFECT_SOURCE_ASM = 1U << 7
} MinicMachineEffects;

typedef enum MinicMachineInstructionKind {
    MINIC_MACHINE_INSTRUCTION_INVALID = 0,
    MINIC_MACHINE_INSTRUCTION_SELECTED,
    MINIC_MACHINE_INSTRUCTION_SOURCE_ASM
} MinicMachineInstructionKind;

typedef struct MinicMachineInstruction {
    MinicMachineInstructionKind kind;
    uint32_t opcode; /* target-scoped; zero reserved for source ASM */
    unsigned int effects;
    /* Exact-count owned arrays in appended instructions. Input arrays passed
     * to append are borrowed and may be stack-allocated. */
    MinicMachineOperand *operands;
    size_t operand_count;
    /* Implicit physical defs/uses/clobbers; never inferred from a C type. */
    MinicMachineRegisterEffect *implicit;
    size_t implicit_count;
    /* The original Core position is diagnostic-only; no Core pointers live
     * inside a machine function. */
    bool has_core_origin;
    size_t core_block_id;
    size_t core_instruction_id;
    /* Only SOURCE_ASM owns an opaque source template. Selected opcodes may
     * never use this field to bypass feature or opcode validation. */
    char *source_asm_template;
    size_t source_asm_length; /* exact source byte length, excluding final NUL */
    /* Source ASM goto/branch is an explicit physical control-flow decision;
     * the target policy must independently authorize its flags and labels. */
    bool source_asm_terminates_block;
    bool source_asm_permits_fallthrough;
} MinicMachineInstruction;

typedef struct MinicMachineBlock {
    size_t id;
    bool machine_auxiliary; /* target inserted; still obeys physical CFG */
    char *exported_label;
    bool indirect_entry;
    bool has_fallthrough;
    size_t fallthrough_target;
    size_t *successors;
    size_t successor_count;
    size_t successor_capacity;
    MinicMachineInstruction *instructions;
    size_t instruction_count;
    size_t instruction_capacity;
} MinicMachineBlock;

typedef struct MinicMachineFunction {
    const MinicTargetInfo *target; /* immutable target registry descriptor */
    MinicTargetArchitecture architecture;
    char *name;
    uint64_t generated_features; /* not the features accepted by source ASM */
    bool has_dynamic_stack; /* alloca/variable stack pointer movement */
    MinicFunctionCallingConvention calling_convention;
    size_t frame_size;
    size_t frame_alignment;
    MinicMachineFrameObject *frame_objects;
    size_t frame_object_count;
    size_t frame_object_capacity;
    MinicMachineBlock *blocks;
    size_t block_count;
    size_t block_capacity;
} MinicMachineFunction;

bool minic_machine_function_initialize(MinicMachineFunction *,
    const MinicTargetInfo *, const char *name, uint64_t generated_features);
bool minic_machine_function_add_frame_object(MinicMachineFunction *,
    const MinicMachineFrameObject *, size_t *id);
bool minic_machine_function_add_block(MinicMachineFunction *,
    const char *exported_label, bool indirect_entry, size_t *id);
bool minic_machine_block_add_successor(MinicMachineFunction *, size_t from,
    size_t to);
bool minic_machine_block_set_fallthrough(MinicMachineFunction *, size_t from,
    size_t to);
/* Deep-copies the instruction including symbol, constraint and source-ASM
 * strings. The caller may immediately destroy its input buffers. */
bool minic_machine_block_append_instruction(MinicMachineFunction *,
    size_t block_id, const MinicMachineInstruction *);
void minic_machine_function_destroy(MinicMachineFunction *);

#endif
