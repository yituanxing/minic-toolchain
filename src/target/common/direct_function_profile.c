#include "target/common/direct_function_profile.h"
#include "target/common/core_function_facts.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/lp64_scalar.h"

#include <string.h>

/* Shared source/Core admission, with the original ISA-owned ABI selection.
 * AAPCS64 uses independent GP/FP fixed-parameter banks; LP64D's current
 * linear variadic cursor rejects fixed double and counts the rest as GP. */
MinicDirectLP64ProfileResult minic_multi_direct_lp64_profile_function(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicFunction *decl, const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicDirectLP64AbiPolicy *abi,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64ArgumentPlan *incoming, MinicDirectLP64FunctionProfile *profile) {
    size_t i;

    if (program == NULL || target == NULL || decl == NULL || fn == NULL || facts == NULL ||
        incoming == NULL || profile == NULL || indirect_record == NULL ||
        !minic_direct_lp64_abi_policy_valid(abi))
        return MINIC_DIRECT_LP64_PROFILE_PARAMETER_SHAPE;
    (void)memset(profile, 0, sizeof(*profile));
    profile->variadic_address_count = facts->variadic_address_count;
    profile->has_dynamic_stack = facts->uses_dynamic_stack_alloc;
    if (decl->is_variadic) {
        for (i = 0U; i < fn->parameter_count; ++i)
            if (!abi->variadic_named_double_supported &&
                minic_type_is_double(fn->parameter_types[i]))
                return MINIC_DIRECT_LP64_PROFILE_VARIADIC_FIXED_FLOAT;
    }
    /* The incoming ABI classifier is the source of truth for consumed
     * register/stack slots. One fixed record can occupy two GARs, a fixed
     * floating argument can occupy an FAR, and a hidden indirect result
     * consumes the first LoongArch GAR. Counting C parameters as GARs
     * would position va_start at the wrong byte address. */
    for (i = 0U; i < fn->parameter_count; ++i) {
        size_t record_size, record_slots;
        if (!minic_lp64_valid_parameter_type(program, target, fn->parameter_types[i]) &&
            !minic_multi_empty_record(program, target, fn->parameter_types[i]) &&
            !minic_multi_gp_record(program, target, fn->parameter_types[i],
                                   &record_size, &record_slots) &&
            !indirect_record(program, target, fn->parameter_types[i], &record_size))
            return MINIC_DIRECT_LP64_PROFILE_PARAMETER_SHAPE;
    }
    if (!minic_lp64_plan_incoming_parameters(
            program, target, fn, abi, indirect_record, incoming))
        return MINIC_DIRECT_LP64_PROFILE_PARAMETER_SHAPE;
    if (abi->variadic_cursor == MINIC_DIRECT_LP64_VARIADIC_ABI_STATE) {
        size_t starts = 0U, cursors = 0U;
        /* AAPCS64's mutable va_list stores the NEXT available GR/VR
         * register offset, not the number of named C parameters. A fixed
         * 16-byte record consumes two GPRs; a zero-sized record consumes
         * none; a wide scalar can skip an odd register to use an even pair.
         * Derive both bank frontiers exclusively from the incoming ABI
         * argument placement, the same classifier used by the caller. */
        for (i = 0U; i < incoming->argument_count; ++i) {
            const MinicLP64ArgumentLocation *loc = &incoming->arguments[i];
            if (loc->gp_registers != 0U &&
                loc->gp_first + loc->gp_registers > profile->fixed_gp_count)
                profile->fixed_gp_count = loc->gp_first + loc->gp_registers;
            if (loc->fp_registers != 0U &&
                loc->fp_first + loc->fp_registers > profile->fixed_fp_count)
                profile->fixed_fp_count = loc->fp_first + loc->fp_registers;
            /* An entire argument may spill because the remaining bank has
             * insufficient registers. Its unused trailing registers are
             * then inaccessible to subsequent anonymous arguments. A
             * stack-passed FP/long-double only exhausts VRs; all other
             * nonempty argument kinds exhaust GRs. */
            if (loc->stack_slots != 0U) {
                if (loc->kind == MINIC_LP64_ARGUMENT_FP ||
                    (loc->kind == MINIC_LP64_ARGUMENT_WIDE &&
                     abi->long_double_in_fp_register &&
                     minic_type_is_long_double(fn->parameter_types[i]))) {
                    profile->fixed_fp_count = abi->fp_argument_registers;
                } else {
                    profile->fixed_gp_count = abi->gp_argument_registers;
                }
            }
        }
        for (i = 0U; i < fn->instruction_count; ++i) {
            const MinicCoreInstruction *ins = &fn->instructions[i];
            if (ins->kind != MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS)
                continue;
            ++cursors;
            if (!ins->value.variadic_address.copy_destination)
                ++starts;
        }
        /* A function may legitimately restart the same va_list after
         * va_end (configfs config_item_set_name does two independent
         * formatting passes). Each va_start has its own Core cursor origin;
         * the AAPCS64 frame planner already allocates a separate 32-byte
         * mutable tag for every origin. Rejecting starts > 1 here was an
         * obsolete preflight limit, not an ABI restriction. */
        /* AAPCS64's two register banks may be exhausted by named arguments.
         * In that case va_start begins with zero register offsets and an
         * __stack cursor beyond the named stack slots; the incoming plan
         * already tracks those slots. Rejecting >8 fixed arguments here
         * suppressed a valid ABI case and hid cursor-placement bugs. */
        if (cursors != profile->variadic_address_count ||
            (starts != 0U && !decl->is_variadic))
            return MINIC_DIRECT_LP64_PROFILE_VARIADIC_SHAPE;
    } else if (profile->variadic_address_count != 0U) {
        size_t starts = 0U, cursors = 0U, consumed_gp = 0U;
        size_t indirect_bytes = 0U;
        const bool hidden_indirect_result =
            minic_type_is_record(fn->return_type) &&
            indirect_record(program, target, fn->return_type,
                            &indirect_bytes) && indirect_bytes > 16U;
        consumed_gp = hidden_indirect_result ? 1U : 0U;
        for (i = 0U; i < incoming->argument_count; ++i) {
            const MinicLP64ArgumentLocation *loc = &incoming->arguments[i];
            if (loc->gp_registers != 0U &&
                loc->gp_first + loc->gp_registers > consumed_gp)
                consumed_gp = loc->gp_first + loc->gp_registers;
        }
        /* Once fixed parameters spill, the GP register save bank ends at
         * the entry stack pointer, followed by the caller's contiguous
         * stack slots. The pointer may legitimately be past all eight GARs. */
        profile->fixed_gp_count = incoming->stack_slots != 0U
            ? abi->gp_argument_registers + incoming->stack_slots : consumed_gp;
        for (i = 0U; i < fn->instruction_count; ++i) {
            const MinicCoreInstruction *ins = &fn->instructions[i];
            if (ins->kind != MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS)
                continue;
            ++cursors;
            if (!ins->value.variadic_address.copy_destination) ++starts;
        }
        /* LP64D uses independent scalar pointer cursors, not AAPCS64's
         * mutable 32-byte va_list state. Multiple GNU va_start executions
         * are valid and each receives the same ABI starting address.
         * A Core ABI-state va_copy must never be confused with va_start. */
        if (!decl->is_variadic || cursors != profile->variadic_address_count ||
            starts != cursors || starts == 0U)
            return MINIC_DIRECT_LP64_PROFILE_VARIADIC_SHAPE;
    }
    return MINIC_DIRECT_LP64_PROFILE_OK;
}
