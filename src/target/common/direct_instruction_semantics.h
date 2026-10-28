#ifndef MINIC_TARGET_COMMON_DIRECT_INSTRUCTION_SEMANTICS_H
#define MINIC_TARGET_COMMON_DIRECT_INSTRUCTION_SEMANTICS_H

#include "target/common/direct_call_plan.h"
#include "target/common/lp64_frame.h"

/* Only ISA-independent, already-shared AAPCS64/LP64D Core admission.
 * Machine addressing, byte transfers, registers, and call/return ABI
 * stay independently owned by each target emitter. */
bool minic_multi_direct_record_copy_extent(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *bytes);

bool minic_multi_direct_record_load_extent(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreInstruction *,
    const MinicLP64FramePlan *, MinicCoreValueId *source,
    size_t *destination_offset, size_t *bytes);

bool minic_multi_direct_parameter_object_location(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64ArgumentPlan *incoming_plan,
    const MinicLP64FramePlan *frame_plan,
    const MinicLP64ArgumentLocation **location, size_t *object_offset);

/* Machine-agnostic contract for the already-existing Core fixed-register
 * SSA read. TargetInfo admits only named architectural registers actually
 * owned by a machine emitter; this validates binding provenance and LP64
 * scalar shape, never picks a register or emits a machine instruction.
 * x86-64 and AArch64 both call this same validator. */
/* Shared Core meaning for ELF-ABI return address level 0, reused by both
 * AAPCS64 and LoongArch64 without duplicating source or Core logic.
 * The machine emitter still decides live LR/ra versus its frame spill. */
/* Shared typed Core CALL_FRAME_ADDRESS shape used by all three LP64
 * machines. Native ABI emitters alone decide saved/live PC and frame. */
bool minic_multi_direct_call_frame_level0_shape(
    const MinicCoreFunction *, const MinicCoreInstruction *,
    MinicCoreCallFrameAddressKind);
bool minic_multi_direct_call_frame_return_level0_shape(
    const MinicCoreFunction *, const MinicCoreInstruction *);

/* Shared fixed-register write provenance/SSA/type admission. The native
 * machine is still responsible for choosing register bytes and forbidding
 * unsupported physical-register aliases. */
bool minic_multi_direct_fixed_register_write_shape(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreInstruction *,
    const MinicCoreFixedRegisterBinding **);

bool minic_multi_direct_fixed_register_read_shape(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicCoreInstruction *,
    const MinicCoreFixedRegisterBinding **binding);

/* Wide (128-bit) stores share Core IR shape admission; ISA-specific
 * bytes/register transfer remain target-owned. Compiler barriers are private
 * to the Common machine-family classifier. */
bool minic_multi_direct_wide_store_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan, size_t *value_offset);

/* IEEE source operands, destination type and scalar integer-conversion
 * width/sign form a Core semantic contract shared by AArch64 and LoongArch64.
 * The two ISA emitters still choose register sequences, rounding operations,
 * and calling convention independently. Width/sign are outputs only for
 * INTEGER_TO_DOUBLE and DOUBLE_TO_INTEGER; other kinds admit NULL outputs. */
bool minic_multi_direct_scalar_parameter_location(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64ArgumentPlan *incoming,
    const MinicLP64ArgumentLocation **location);
bool minic_multi_direct_floating_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *integer_width, bool *integer_signed);

/* Private machine-independent admission; register allocation, ISA instruction
 * selection and all native emission stay in the target backends. */
/* Exact direct-LP64 Core pointer/int cast and pointer-order admission. */
bool minic_multi_direct_scalar_bitcast_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool *pointer_to_integer, size_t *source_width,
    bool *source_signed, bool *source_pointer);
bool minic_multi_direct_pointer_less_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins);

bool minic_multi_direct_integer_unary_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed);
bool minic_multi_direct_integer_shift_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *left_width, size_t *right_width,
    bool *left_signed, bool *right_signed);
bool minic_multi_direct_integer_compare_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *left_width, size_t *right_width,
    bool *left_signed, bool *right_signed);
bool minic_multi_direct_scalar_zero_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer);
bool minic_multi_direct_pointer_offset_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t maximum_stride, size_t *index_width, bool *index_signed);

/* Scalar integer binary families keep their existing ABI width rules. */
bool minic_multi_direct_integer_binary_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *left_width, size_t *right_width,
    bool *left_signed, bool *right_signed);

/* Relaxed-only scalar LOAD/ATOMIC_LOAD shape validation. */
bool minic_multi_direct_scalar_load_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer);

/* Wide load pointer and frame-admission: target decides 128-bit transfers. */
bool minic_multi_direct_wide_scalar_load_location(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan, MinicCoreValueId *address,
    size_t *destination_offset);

bool minic_multi_direct_scalar_store_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer);


/* Common checked Core dispatch outcome. Actual register width, memory
 * transport, opcode and ABI remain independently owned by each ISA. */
typedef enum MinicDirectMachineFamily {
    MINIC_DIRECT_MACHINE_COMPILER_BARRIER,
    MINIC_DIRECT_MACHINE_RECORD_COPY,
    MINIC_DIRECT_MACHINE_RECORD_LOAD,
    MINIC_DIRECT_MACHINE_PARAMETER_OBJECT,
    MINIC_DIRECT_MACHINE_MEMORY,
    MINIC_DIRECT_MACHINE_FP,
    MINIC_DIRECT_MACHINE_INTEGER,
    MINIC_DIRECT_MACHINE_ABI,
    MINIC_DIRECT_MACHINE_RESIDUAL
} MinicDirectMachineFamily;
typedef struct MinicDirectMachineInstructionShape {
    MinicDirectMachineFamily family;
    size_t result_offset;
} MinicDirectMachineInstructionShape;

bool minic_multi_direct_machine_instruction_shape(
    const MinicCoreFunction *, const MinicCoreInstruction *,
    bool allow_variadic_double_result,
    MinicDirectMachineInstructionShape *);

#endif
