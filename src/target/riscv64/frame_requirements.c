#include "target/riscv64/frame_requirements.h"
#include "target/common/core_instruction_family.h"
#include "target/common/core_call_view.h"
#include "target/riscv64/abi.h"
#include <stdint.h>

/* Preserve ABI-specific requirement queries, not a simplistic common frame. */
/* M79_CALL_FRAME_RETURN_ADDRESS: a return-address query needs the entry
   value of ra even in a function that has no ordinary Core CALL yet. Save it
   in the prologue whenever either a call or this semantic instruction exists. */
bool minic_riscv64_core_variadic_fixed_prefix_profile(
    const MinicC0Program *program, const MinicCoreFunction *function,
    bool lp64_soft_float, size_t *integer_parameter_count,
    size_t *fixed_stack_slots) {
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t parameter_index;

    if (program == NULL || function == NULL || integer_parameter_count == NULL ||
        fixed_stack_slots == NULL ||
        !minic_riscv64_abi_cursor_initialize_for_return_profile(
            program, function->return_type, lp64_soft_float,
            &cursor, &return_value)) {
        return false;
    }
    (void)return_value;
    for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
        MinicRiscv64AbiArgumentLocation location;

        if (!minic_riscv64_abi_place_argument(
                program, function->parameter_types[parameter_index], true, &cursor, &location)) {
            return false;
        }
    }
    /* RV64 varargs are contiguous after the named prefix. If integer argument
       registers remain, preserve them in the callee frame as before. Once the
       named prefix has exhausted a0-a7, the first unnamed argument starts
       directly after the named incoming stack slots. Keep mixed register/stack
       prefixes fail-closed unless all integer argument registers are consumed. */
    return minic_riscv64_abi_variadic_prefix_from_cursor(
        &cursor, integer_parameter_count, fixed_stack_slots);
}

/* The RV64 psABI parameter cursor is target-owned. CoreCallView supplies
 * the same checked VALUE/OBJECT argument identities consumed by all five
 * architectures, rather than another RV64-only type resolver. */
bool minic_riscv64_core_variadic_fixed_prefix(
    const MinicC0Program *program, const MinicCoreFunction *function,
    size_t *integer_parameter_count, size_t *fixed_stack_slots) {
    return minic_riscv64_core_variadic_fixed_prefix_profile(
        program, function, false, integer_parameter_count, fixed_stack_slots);
}

/* Exactly one psABI outgoing-stack computation per semantic CALL view.
 * Direct and indirect source CALLs share this ABI placement policy. */
static bool rv64_call_stack_slots(
    const MinicC0Program *program, const MinicCoreFunction *function,
    bool lp64_soft_float, const MinicCoreCallView *call,
    size_t *maximum_stack_slots) {
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t argument_index;
    if (call == NULL || maximum_stack_slots == NULL ||
        !minic_riscv64_abi_cursor_initialize_for_return_profile(
            program, call->return_type, lp64_soft_float,
            &cursor, &return_value))
        return false;
    (void)return_value;
    for (argument_index = 0U; argument_index < call->argument_count; ++argument_index) {
        MinicRiscv64AbiArgumentLocation location;
        MinicType actual_type, placement_type;
        const bool is_fixed_parameter =
            argument_index < call->parameter_count;
        if (!minic_core_call_view_argument_type(
                function, call, argument_index, &actual_type))
            return false;
        /* The psABI classifies fixed formals by their declared type.
         * Unnamed variadic arguments use their actual promoted Core type. */
        placement_type = is_fixed_parameter
            ? call->parameter_types[argument_index] : actual_type;
        if (!minic_riscv64_abi_place_argument(
                program, placement_type, is_fixed_parameter, &cursor, &location))
            return false;
    }
    if (cursor.stack_slot_count > *maximum_stack_slots)
        *maximum_stack_slots = cursor.stack_slot_count;
    return true;
}

bool minic_riscv64_core_call_outgoing_stack_size_profile(
    const MinicC0Program *program, const MinicCoreFunction *function,
    bool lp64_soft_float, const MinicCoreCallIndex *semantic_calls,
    size_t *result) {
    size_t i, maximum_stack_slots = 0U;
    if (program == NULL || function == NULL || result == NULL ||
        (function->instruction_count != 0U && function->instructions == NULL))
        return false;
    if (semantic_calls != NULL) {
        size_t previous_id = SIZE_MAX;
        if (semantic_calls->count > function->instruction_count ||
            (semantic_calls->count != 0U && semantic_calls->sites == NULL))
            return false;
        /* In production the Common owner already scanned/verified all
         * opcodes. Do not rescan N non-CALL opcodes in the RV64 frame. */
        for (i = 0U; i < semantic_calls->count; ++i) {
            const MinicCoreCallSite *site = &semantic_calls->sites[i];
            if (site->instruction_id >= function->instruction_count ||
                (previous_id != SIZE_MAX && site->instruction_id <= previous_id) ||
                !minic_core_instruction_is_call(
                    function->instructions[site->instruction_id].kind) ||
                !rv64_call_stack_slots(program, function, lp64_soft_float,
                                       &site->view, &maximum_stack_slots))
                return false;
            previous_id = site->instruction_id;
        }
    } else {
        /* Existing standalone entry points still accept no Common plan. */
        for (i = 0U; i < function->instruction_count; ++i) {
            const MinicCoreInstruction *ins = &function->instructions[i];
            MinicCoreCallView view;
            if (!minic_core_instruction_is_call(ins->kind)) continue;
            if (!minic_core_call_view_build(function, ins, &view) ||
                !rv64_call_stack_slots(program, function, lp64_soft_float,
                                       &view, &maximum_stack_slots))
                return false;
        }
    }
    if (maximum_stack_slots > SIZE_MAX / 8U) return false;
    *result = maximum_stack_slots * 8U;
    return true;
}
bool minic_riscv64_core_call_outgoing_stack_size(
    const MinicC0Program *program, const MinicCoreFunction *function,
    size_t *result) {
    return minic_riscv64_core_call_outgoing_stack_size_profile(
        program, function, false, NULL, result);
}

