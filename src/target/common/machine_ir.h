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
    MINIC_MACHINE_OPERAND_BLOCK_ADDRESS,
    MINIC_MACHINE_OPERAND_CURRENT_LOCATION /* GNU `.` in typed data fixup */
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
    /* Original named input/output or goto label (%[operand], %l[name]);
     * optional for positional operands and owned by MachineFunction. */
    char *asm_name;
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
    MINIC_MACHINE_INSTRUCTION_SOURCE_ASM,
    /* A typed, target-independent position marker. This is not an opcode,
     * opaque assembly escape hatch or a machine-CFG block boundary. */
    MINIC_MACHINE_INSTRUCTION_LOCAL_LABEL,
    MINIC_MACHINE_INSTRUCTION_DIRECTIVE,
    /* Core compiler-ordering effect, not a native opcode or source ASM.
     * It intentionally prints no GNU bytes and carries exact Core origin. */
    MINIC_MACHINE_INSTRUCTION_COMPILER_BARRIER
} MinicMachineInstructionKind;

/* The only assembler-side operations inside a machine function are typed
 * layout/fixup fragments. Neither arbitrary .s text nor selected opcodes
 * may be inserted through this path. ELF section ownership remains with
 * ModuleIR; these directives are machine-specific outlined fragments. */
typedef enum MinicMachineDirectiveKind {
    MINIC_MACHINE_DIRECTIVE_INVALID = 0,
    MINIC_MACHINE_DIRECTIVE_PUSH_SECTION,
    MINIC_MACHINE_DIRECTIVE_POP_SECTION,
    MINIC_MACHINE_DIRECTIVE_DATA
} MinicMachineDirectiveKind;

typedef enum MinicMachineDirectiveSectionFlags {
    MINIC_MACHINE_SECTION_ALLOC = 1U << 0,
    MINIC_MACHINE_SECTION_WRITE = 1U << 1,
    MINIC_MACHINE_SECTION_EXEC = 1U << 2,
    MINIC_MACHINE_SECTION_LINK_ORDER = 1U << 3
} MinicMachineDirectiveSectionFlags;

typedef enum MinicMachineDirectiveSectionType {
    MINIC_MACHINE_SECTION_TYPE_NONE = 0,
    MINIC_MACHINE_SECTION_TYPE_PROGBITS,
    MINIC_MACHINE_SECTION_TYPE_NOBITS
} MinicMachineDirectiveSectionType;

typedef struct MinicMachineDirective {
    MinicMachineDirectiveKind kind;
    /* PUSH_SECTION only: lexical ELF section identifier, not source asm. */
    char *section_name;
    unsigned int section_flags;
    MinicMachineDirectiveSectionType section_type;
    char *link_order_symbol;
    /* DATA only: 1/2/4/8 bytes, a typed lhs and optional subtraction rhs
     * in the instruction's owned operand array. */
    unsigned int data_width_bytes;
} MinicMachineDirective;

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
    /* Immutable target-recognized Linux/source ASM descriptor imported from
     * CoreInlineAsm. Zero ID means no special machine-form claim. */
    uint32_t source_asm_machine_form_id;
    uint32_t source_asm_machine_form_variant;
    /* A target-qualified source-ASM occurrence may instead lower into 1:N
     * genuine SELECTED opcodes. Exactly one semantic opcode carries this
     * seal; it owns no source template and needs independent target opcode
     * validation before Core coverage can credit the original ASM effect. */
    bool has_selected_source_asm_seal;
    /* GNU `%=` requires an explicit translation-unit unique occurrence ID.
     * It must come from source/Module lowering, never a serializer hash. */
    bool has_source_asm_unique_id;
    uint64_t source_asm_unique_id;
    /* Present only for LOCAL_LABEL. Allows labels inside the same physical
     * block (GNU exception/fixup pairs, local FP conversion branches). */
    char *local_label_name;
    MinicMachineDirective *directive;
    /* Source ASM goto/branch is an explicit physical control-flow decision;
     * the target policy must independently authorize its flags and labels. */
    bool source_asm_terminates_block;
    bool source_asm_permits_fallthrough;
} MinicMachineInstruction;

typedef struct MinicMachineBlock {
    size_t id;
    bool machine_auxiliary; /* target inserted; still obeys physical CFG */
    /* Exactly one primary machine block identifies each reachable Core block.
     * Target-created prologue/branch islands are auxiliary and have no Core
     * block identity; this is independent of per-instruction provenance. */
    bool has_core_block_origin;
    size_t core_block_id;
    char *exported_label;
    /* Several externally referenced names can resolve to one block entry,
     * for example a Core block name and an address-taken label. */
    char **alias_labels;
    size_t alias_count;
    size_t alias_capacity;
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
bool minic_machine_block_add_alias_label(MinicMachineFunction *,
    size_t block_id, const char *alias_label);
bool minic_machine_block_add_successor(MinicMachineFunction *, size_t from,
    size_t to);
bool minic_machine_block_set_fallthrough(MinicMachineFunction *, size_t from,
    size_t to);
/* Deep-copies the instruction including symbol, constraint and source-ASM
 * strings. The caller may immediately destroy its input buffers. */
bool minic_machine_block_append_instruction(MinicMachineFunction *,
    size_t block_id, const MinicMachineInstruction *);
/* Erase one owned physical instruction while retaining all block identities,
 * successors, Core provenance of remaining instructions, and ELF sections. */
bool minic_machine_block_erase_instruction(MinicMachineFunction *,
    size_t block_id, size_t instruction_id);
void minic_machine_function_destroy(MinicMachineFunction *);

#endif
