#include "target/common/direct_function_profile.h"
#include "target/common/core_function_facts.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/lp64_scalar.h"

#include <string.h>

/* Shared sealed-Core admission, with the original ISA-owned ABI selection.
 * AAPCS64 uses independent GP/FP fixed-parameter banks; LP64D's current
 * linear variadic cursor rejects fixed double and counts the rest as GP. */
MinicDirectLP64ProfileResult minic_multi_direct_lp64_profile_function(
    const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicDirectLP64AbiPolicy *abi,
    MinicLP64ArgumentPlan *incoming, MinicDirectLP64FunctionProfile *profile) {
    size_t i, starts, cursors;

    if (fn == NULL || fn->type_context == NULL || facts == NULL ||
        incoming == NULL || profile == NULL ||
        !minic_direct_lp64_abi_policy_valid(abi))
        return MINIC_DIRECT_LP64_PROFILE_PARAMETER_SHAPE;
    if ((minic_core_function_parameter_count(fn) != 0U && minic_core_function_parameter_types(fn) == NULL) ||
        abi->core_indirect_record == NULL ||
        abi->core_indirect_return == NULL)
        return MINIC_DIRECT_LP64_PROFILE_PARAMETER_SHAPE;
    starts = facts->variadic_start_count;
    cursors = facts->variadic_address_count;
    (void)memset(profile, 0, sizeof(*profile));
    profile->variadic_address_count = facts->variadic_address_count;
    profile->has_dynamic_stack = facts->uses_dynamic_stack_alloc;
    if (minic_core_function_is_variadic(fn)) {
        for (i = 0U; i < minic_core_function_parameter_count(fn); ++i)
            if (!abi->variadic_named_double_supported &&
                minic_lp64_core_type_float_format(fn->type_context,
                    minic_core_function_parameter_types(fn)[i], MINIC_CORE_FLOAT_BINARY64))
                return MINIC_DIRECT_LP64_PROFILE_VARIADIC_FIXED_FLOAT;
    }
    /* Incoming planning checks type validity and classifies each ABI
     * parameter in one pass; only its completed locations define fixed
     * GAR/FAR and stack consumption for a subsequent va_start. */
    if (!minic_lp64_plan_incoming_parameters(fn, abi, incoming))
        return MINIC_DIRECT_LP64_PROFILE_PARAMETER_SHAPE;
    /* Core FunctionFacts already counted every cursor and va_start while
     * collecting the result IDs. Do not rescan the immutable instruction
     * array just to recover the same semantic counts. */
    if (abi->variadic_cursor == MINIC_DIRECT_LP64_VARIADIC_ABI_STATE) {
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
                    loc->kind == MINIC_LP64_ARGUMENT_VECTOR ||
                    (loc->kind == MINIC_LP64_ARGUMENT_WIDE &&
                     abi->long_double_in_fp_register &&
                     minic_lp64_core_type_float_format(fn->type_context,
                         minic_core_function_parameter_types(fn)[i], MINIC_CORE_FLOAT_BINARY128))) {
                    profile->fixed_fp_count = abi->fp_argument_registers;
                } else {
                    profile->fixed_gp_count = abi->gp_argument_registers;
                }
            }
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
            (starts != 0U && !minic_core_function_is_variadic(fn)))
            return MINIC_DIRECT_LP64_PROFILE_VARIADIC_SHAPE;
    } else if (profile->variadic_address_count != 0U) {
        size_t consumed_gp =
            minic_lp64_core_indirect_result(fn->type_context,
                minic_core_function_return_type(fn), abi->core_indirect_return)
                ? 1U : 0U;
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
        /* LP64D uses independent scalar pointer cursors, not AAPCS64's
         * mutable 32-byte va_list state. Multiple GNU va_start executions
         * are valid and each receives the same ABI starting address.
         * A Core ABI-state va_copy must never be confused with va_start. */
        if (!minic_core_function_is_variadic(fn) || cursors != profile->variadic_address_count ||
            starts != cursors || starts == 0U)
            return MINIC_DIRECT_LP64_PROFILE_VARIADIC_SHAPE;
    }
    return MINIC_DIRECT_LP64_PROFILE_OK;
}
