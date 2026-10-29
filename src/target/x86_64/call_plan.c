#include "target/x86_64/call_plan.h"
#include "target/x86_64/abi.h"
#include "target/data_layout.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* One ABI classifier for both incoming and outgoing SysV argument streams.
 * A two-GPR record is indivisible: both registers or both stack slots.
 * x87 long double starts on an even eightbyte stack index. */
static bool x86_sysv_place(const MinicC0Program *program,
                          const MinicTargetInfo *target, MinicType type,
                          MinicX86ArgCursor *cursor, MinicX86ArgLocation *loc) {
    size_t bytes;
    bool sign;
    if (program == NULL || target == NULL || cursor == NULL || loc == NULL)
        return false;
    (void)memset(loc, 0, sizeof(*loc));
    loc->gp_index = cursor->gp_used;
    loc->fp_index = cursor->fp_used;
    loc->stack_index = cursor->stack_slots;
    if (minic_type_is_record(type)) {
        if (minic_x86_64_one_gp_record(program, target, type, &bytes)) {
            loc->kind = MINIC_X86_ARG_RECORD_ONE_GP;
            loc->record_bytes = bytes;
            if (cursor->gp_used < 6U) ++cursor->gp_used;
            else { loc->on_stack = true; ++cursor->stack_slots; }
        } else if (minic_x86_64_two_gp_record(program, target, type)) {
            loc->kind = MINIC_X86_ARG_RECORD_TWO_GP;
            if (cursor->gp_used <= 4U) cursor->gp_used += 2U;
            else { loc->on_stack = true; cursor->stack_slots += 2U; }
        } else {
            /* SysV AMD64 classifies any aggregate larger than 16 bytes as
             * MEMORY. Smaller unsupported SSE or mixed classes must remain
             * explicit failures. Keep emission bounded to avoid huge stubs. */
            size_t alignment = 0U;
            const MinicRecord *record = minic_c0_program_record(
                program, type.record_id);
            bytes = 0U;
            if (minic_data_layout_type(target->data_layout, program, type,
                                       &bytes, &alignment) && bytes == 0U &&
                record != NULL && record->is_complete &&
                record->field_count == 0U) {
                /* A GNU C `struct empty {}` is omitted from the SysV
                 * argument stream, including through inlined functions. */
                loc->kind = MINIC_X86_ARG_RECORD_EMPTY;
                return true;
            }
            if (!minic_data_layout_type(target->data_layout, program, type,
                                        &bytes, &alignment) ||
                bytes <= 16U || bytes > 4096U ||
                alignment == 0U || alignment > 16U) {
                if (getenv("MINIC_M1_TRACE") != NULL)
                    (void)fprintf(stderr,
                        "MINIC_X86_ARG_RECORD_CLASS_REJECT record=%zu "
                        "bytes=%zu align=%zu\n",
                        (size_t)type.record_id, bytes, alignment);
                return false;
            }
            loc->kind = MINIC_X86_ARG_RECORD_MEMORY;
            loc->record_bytes = bytes;
            loc->on_stack = true;
            if (alignment > 8U) {
                if (cursor->stack_slots == SIZE_MAX) return false;
                cursor->stack_slots += cursor->stack_slots & 1U;
            }
            loc->stack_index = cursor->stack_slots;
            if (cursor->stack_slots > SIZE_MAX - (bytes + 7U) / 8U)
                return false;
            cursor->stack_slots += (bytes + 7U) / 8U;
        }
    } else if (minic_type_is_float(type) || minic_type_is_double(type)) {
        loc->kind = MINIC_X86_ARG_SSE;
        if (cursor->fp_used < 8U) ++cursor->fp_used;
        else { loc->on_stack = true; ++cursor->stack_slots; }
    } else if (minic_type_is_long_double(type)) {
        loc->kind = MINIC_X86_ARG_X87_MEMORY;
        cursor->stack_slots += cursor->stack_slots & 1U;
        loc->stack_index = cursor->stack_slots;
        loc->on_stack = true;
        cursor->stack_slots += 2U;
    } else if (minic_type_is_int128_integer(type)) {
        /* The scalar TI ABI must not be treated as a record or as one
         * eightbyte. Both INTEGER registers are consumed atomically.
         * Stack fallback is 16-byte aligned, unlike ordinary scalars. */
        loc->kind = MINIC_X86_ARG_I128_TWO_GP;
        if (cursor->gp_used <= 4U) {
            cursor->gp_used += 2U;
        } else {
            loc->on_stack = true;
            cursor->stack_slots += cursor->stack_slots & 1U;
            loc->stack_index = cursor->stack_slots;
            cursor->stack_slots += 2U;
        }
    } else {
        if (!minic_x86_64_scalar_width(program, target, type, &bytes, &sign))
            return false;
        loc->kind = MINIC_X86_ARG_INTEGER;
        if (cursor->gp_used < 6U) ++cursor->gp_used;
        else { loc->on_stack = true; ++cursor->stack_slots; }
    }
    return cursor->stack_slots <= (size_t)INT32_MAX / 8U;
}

/* Microsoft x64 uses the same four *positional* slots for GP and SSE
 * arguments. Stack arguments start after the compulsory 32-byte home area;
 * carrying their source position as stack_index makes position four land at
 * 32(%rsp) on calls and at 48(%rbp) on function entry. */
static bool x86_ms_x64_place(const MinicC0Program *program,
                            const MinicTargetInfo *target, MinicType type,
                            size_t position, MinicX86ArgLocation *loc) {
    size_t bytes;
    bool sign;
    if (program == NULL || target == NULL || loc == NULL ||
        position > ((size_t)INT32_MAX - 16U) / 8U ||
        minic_type_is_record(type) || minic_type_is_int128_integer(type) ||
        minic_type_is_long_double(type)) return false;
    (void)memset(loc, 0, sizeof(*loc));
    loc->gp_index = position;
    loc->fp_index = position;
    loc->stack_index = position;
    loc->on_stack = position >= 4U;
    if (minic_type_is_float(type) || minic_type_is_double(type)) {
        loc->kind = MINIC_X86_ARG_SSE;
        return true;
    }
    if (!minic_x86_64_scalar_width(program, target, type, &bytes, &sign))
        return false;
    loc->kind = MINIC_X86_ARG_INTEGER;
    return true;
}

static bool x86_ms_x64_scalar_return(const MinicC0Program *program,
                                    const MinicTargetInfo *target,
                                    MinicType type) {
    size_t bytes;
    bool sign;
    return minic_type_is_void(type) || minic_type_is_float(type) ||
           minic_type_is_double(type) ||
           minic_x86_64_scalar_width(program, target, type, &bytes, &sign);
}

static bool x86_sysv_plan_alloc(size_t count, MinicX86ArgumentPlan *plan) {
    if (plan == NULL || plan->locations != NULL ||
        plan->argument_count != 0U ||
        count > SIZE_MAX / sizeof(*plan->locations)) return false;
    if (count != 0U) {
        plan->locations = (MinicX86ArgLocation *)calloc(
            count, sizeof(*plan->locations));
        if (plan->locations == NULL) return false;
    }
    plan->argument_count = count;
    return true;
}

bool minic_x86_64_plan_incoming_arguments(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicX86ArgumentPlan *plan) {
    MinicX86ArgCursor cursor = {0};
    size_t i;
    if (program == NULL || target == NULL || fn == NULL || plan == NULL ||
        !minic_function_calling_convention_valid(fn->calling_convention) ||
        (fn->parameter_count != 0U && fn->parameter_types == NULL) ||
        !x86_sysv_plan_alloc(fn->parameter_count, plan)) return false;
    plan->calling_convention = fn->calling_convention;
    if (fn->calling_convention == MINIC_CALLING_CONVENTION_MS_X64) {
        if (!x86_ms_x64_scalar_return(program, target, fn->return_type))
            goto failed;
        for (i = 0U; i < fn->parameter_count; ++i)
            if (!x86_ms_x64_place(program, target, fn->parameter_types[i],
                                  i, &plan->locations[i])) goto failed;
        if (fn->parameter_count > 4U)
            cursor.stack_slots = fn->parameter_count - 4U;
        plan->end = cursor;
        return true;
    }
    {
        size_t result_bytes;
        if (minic_x86_64_memory_result_record(
                program, target, fn->return_type, &result_bytes))
            cursor.gp_used = 1U;
    }
    for (i = 0U; i < fn->parameter_count; ++i)
        if (!x86_sysv_place(program, target, fn->parameter_types[i],
                             &cursor, &plan->locations[i])) goto failed;
    plan->end = cursor;
    return true;
failed:
    minic_x86_64_argument_plan_destroy(plan);
    return false;
}

bool minic_x86_64_plan_outgoing_arguments(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreCallView *view,
    MinicX86ArgumentPlan *plan) {
    MinicX86ArgCursor cursor = {0};
    size_t i, bytes;
    if (program == NULL || target == NULL || fn == NULL ||
        view == NULL || plan == NULL ||
        view->parameter_count > view->argument_count ||
        (view->argument_count != 0U && view->arguments == NULL) ||
        (view->parameter_count != 0U && view->parameter_types == NULL) ||
        !minic_function_calling_convention_valid(view->calling_convention) ||
        !x86_sysv_plan_alloc(view->argument_count, plan)) return false;
    plan->calling_convention = view->calling_convention;
    if (view->calling_convention == MINIC_CALLING_CONVENTION_MS_X64) {
        /* Win64 consumes one slot per source parameter: GP and XMM registers
         * have the SAME four positional indices, unlike SysV independent
         * integer/FP cursors. Every call, including zero-arg, reserves 32B
         * home space. Slots 4+ occupy the stack above that home space. */
        if (view->is_variadic ||
            !x86_ms_x64_scalar_return(program, target, view->return_type) ||
            (view->argument_count > ((size_t)INT32_MAX - 47U) / 8U))
            goto failed;
        plan->is_variadic = false;
        for (i = 0U; i < view->argument_count; ++i) {
            MinicType type;
            if (!minic_core_call_view_argument_type(fn, view, i, &type))
                goto failed;
            if (i < view->parameter_count)
                type = view->parameter_types[i];
            if (!x86_ms_x64_place(program, target, type, i,
                                  &plan->locations[i]))
                goto failed;
        }
        if (view->argument_count > 4U)
            cursor.stack_slots = view->argument_count - 4U;
        bytes = 32U + cursor.stack_slots * 8U;
        plan->stack_bytes = (bytes + 15U) & ~(size_t)15U;
        plan->end = cursor;
        return true;
    }
    {
        size_t result_bytes;
        if (view->return_form == MINIC_CORE_CALL_RETURN_OBJECT &&
            minic_x86_64_memory_result_record(
                program, target, view->return_type, &result_bytes))
            cursor.gp_used = 1U;
    }
    plan->is_variadic = view->is_variadic;
    for (i = 0U; i < view->argument_count; ++i) {
        MinicType type;
        if (!minic_core_call_view_argument_type(fn, view, i, &type))
            goto failed;
        /* SysV placement intentionally uses the fixed declared type.
         * Shared Core checks already enforce aggregate identity. */
        if (i < view->parameter_count)
            type = view->parameter_types[i];
        if (!x86_sysv_place(program, target, type, &cursor, &plan->locations[i]))
            goto failed;
    }
    bytes = cursor.stack_slots * 8U;
    if (bytes > SIZE_MAX - 15U) goto failed;
    plan->stack_bytes = (bytes + 15U) & ~(size_t)15U;
    if (plan->stack_bytes > (size_t)INT32_MAX) goto failed;
    plan->end = cursor;
    return true;
failed:
    minic_x86_64_argument_plan_destroy(plan);
    return false;
}

void minic_x86_64_argument_plan_destroy(MinicX86ArgumentPlan *plan) {
    if (plan == NULL) return;
    free(plan->locations);
    (void)memset(plan, 0, sizeof(*plan));
}

/* The Common visitor supplies validated CALL views. x86 owns only the
 * SysV/Win64 register-and-stack ABI planning, performed once per call rather than in
 * the machine instruction emitter. Standalone emitters retain fallback. */
bool minic_x86_64_outgoing_call_plans_prepare(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreCallIndex *calls,
    MinicX86OutgoingCallPlans *result) {
    size_t i, previous = SIZE_MAX;
    if (program == NULL || target == NULL || fn == NULL || calls == NULL ||
        result == NULL || result->sites != NULL || result->count != 0U ||
        calls->count > fn->instruction_count ||
        (calls->count != 0U && calls->sites == NULL) ||
        (fn->instruction_count != 0U && fn->instructions == NULL) ||
        calls->count > SIZE_MAX / sizeof(*result->sites))
        return false;
    if (calls->count == 0U) return true;
    result->sites = calloc(calls->count, sizeof(*result->sites));
    if (result->sites == NULL) return false;
    result->count = calls->count;
    for (i = 0U; i < calls->count; ++i) {
        size_t id = calls->sites[i].instruction_id;
        const MinicCoreInstruction *ins;
        if (id >= fn->instruction_count ||
            (previous != SIZE_MAX && id <= previous))
            goto fail;
        ins = &fn->instructions[id];
        if (ins->kind != MINIC_CORE_INSTRUCTION_CALL &&
            ins->kind != MINIC_CORE_INSTRUCTION_INDIRECT_CALL)
            goto fail;
        result->sites[i].instruction_id = id;
        if (!minic_x86_64_plan_outgoing_arguments(
                program, target, fn, &calls->sites[i].view,
                &result->sites[i].plan))
            goto fail;
        previous = id;
    }
    return true;
fail:
    minic_x86_64_outgoing_call_plans_destroy(result);
    return false;
}
void minic_x86_64_outgoing_call_plans_destroy(MinicX86OutgoingCallPlans *cache) {
    size_t i;
    if (cache == NULL) return;
    for (i = 0U; i < cache->count; ++i)
        minic_x86_64_argument_plan_destroy(&cache->sites[i].plan);
    free(cache->sites);
    (void)memset(cache, 0, sizeof(*cache));
}
