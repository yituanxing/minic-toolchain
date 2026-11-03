#include "target/x86_64/call_plan.h"
#include "target/x86_64/abi.h"
#include "target/data_layout.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum X86AbiCoreClass {
    X86_ABI_CLASS_INVALID = 0,
    X86_ABI_CLASS_RECORD,
    X86_ABI_CLASS_FLOAT,
    X86_ABI_CLASS_DOUBLE,
    X86_ABI_CLASS_LONG_DOUBLE,
    X86_ABI_CLASS_INT128,
    X86_ABI_CLASS_SCALAR,
    X86_ABI_CLASS_VOID
} X86AbiCoreClass;

static X86AbiCoreClass x86_plan_class(const MinicCoreFunction *fn,
                                     MinicCoreTypeId core_id) {
    const MinicCoreType *type;
    if (fn == NULL || fn->type_context == NULL) return X86_ABI_CLASS_INVALID;
    core_id = minic_core_type_unqualified(fn->type_context, core_id);
    type = minic_core_type_get(fn->type_context, core_id);
    if (type == NULL ||
        (type->kind != MINIC_CORE_TYPE_VOID &&
         (!type->has_layout || type->alignment == 0U)))
        return X86_ABI_CLASS_INVALID;
    switch (type->kind) {
    case MINIC_CORE_TYPE_VOID: return X86_ABI_CLASS_VOID;
    case MINIC_CORE_TYPE_RECORD: return X86_ABI_CLASS_RECORD;
    case MINIC_CORE_TYPE_FLOAT:
        switch (type->value.floating.format) {
        case MINIC_CORE_FLOAT_BINARY32:
            return type->size == 4U ? X86_ABI_CLASS_FLOAT : X86_ABI_CLASS_INVALID;
        case MINIC_CORE_FLOAT_BINARY64:
            return type->size == 8U ? X86_ABI_CLASS_DOUBLE : X86_ABI_CLASS_INVALID;
        case MINIC_CORE_FLOAT_EXTENDED:
            return type->size == 16U ? X86_ABI_CLASS_LONG_DOUBLE
                                     : X86_ABI_CLASS_INVALID;
        default: return X86_ABI_CLASS_INVALID;
        }
    case MINIC_CORE_TYPE_INTEGER:
        if (type->value.integer.bit_width == 128U)
            return type->size == 16U ? X86_ABI_CLASS_INT128
                                     : X86_ABI_CLASS_INVALID;
        return X86_ABI_CLASS_SCALAR;
    case MINIC_CORE_TYPE_POINTER: return X86_ABI_CLASS_SCALAR;
    default: return X86_ABI_CLASS_INVALID;
    }
}

static bool x86_plan_scalar_width(const MinicCoreFunction *fn,
                                  MinicCoreTypeId id,
                                  size_t *bytes, bool *sign) {
    return fn != NULL && fn->type_context != NULL &&
           minic_x86_64_core_scalar_width(fn->type_context, id, bytes, sign);
}

static bool x86_plan_one_gp_record(const MinicCoreFunction *fn,
                                   MinicCoreTypeId id,
                                   size_t *bytes) {
    return fn != NULL && fn->type_context != NULL &&
           minic_x86_64_core_one_gp_record(fn->type_context, id, bytes);
}

static bool x86_plan_two_gp_record(const MinicCoreFunction *fn,
                                   MinicCoreTypeId id) {
    return fn != NULL && fn->type_context != NULL &&
           minic_x86_64_core_two_gp_record(fn->type_context, id);
}

static bool x86_plan_memory_result_record(const MinicCoreFunction *fn,
                                          MinicCoreTypeId id,
                                          size_t *bytes) {
    return fn != NULL && fn->type_context != NULL &&
           minic_x86_64_core_memory_result_record(fn->type_context, id, bytes);
}

/* One ABI classifier for both incoming and outgoing SysV argument streams.
 * A two-GPR record is indivisible: both registers or both stack slots.
 * x87 long double starts on an even eightbyte stack index. */
static bool x86_sysv_place(const MinicCoreFunction *fn,
                          MinicCoreTypeId core_id,
                          MinicX86ArgCursor *cursor, MinicX86ArgLocation *loc) {
    const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
        MINIC_CALLING_CONVENTION_DEFAULT);
    size_t bytes;
    bool sign;
    const X86AbiCoreClass type_class = x86_plan_class(fn, core_id);
    if (abi == NULL || abi->gp_argument_count < 2U ||
        cursor == NULL || loc == NULL ||
        type_class == X86_ABI_CLASS_INVALID || type_class == X86_ABI_CLASS_VOID)
        return false;
    (void)memset(loc, 0, sizeof(*loc));
    loc->gp_index = cursor->gp_used;
    loc->fp_index = cursor->fp_used;
    loc->stack_index = cursor->stack_slots;
    if (type_class == X86_ABI_CLASS_RECORD) {
        if (x86_plan_one_gp_record(fn, core_id, &bytes)) {
            loc->kind = MINIC_X86_ARG_RECORD_ONE_GP;
            loc->record_bytes = bytes;
            if (cursor->gp_used < abi->gp_argument_count) ++cursor->gp_used;
            else { loc->on_stack = true; ++cursor->stack_slots; }
        } else if (x86_plan_two_gp_record(fn, core_id)) {
            loc->kind = MINIC_X86_ARG_RECORD_TWO_GP;
            /* The second eightbyte may be a 1..8-byte object tail.
             * Preserve its exact extent for safe native transport. */
            {
                const MinicCoreType *resolved = minic_core_type_get(
                    fn->type_context,
                    minic_core_type_unqualified(fn->type_context, core_id));
                if (resolved == NULL || !resolved->has_layout) return false;
                loc->record_bytes = resolved->size;
            }
            if (loc->record_bytes < 9U || loc->record_bytes > 16U) return false;
            if (cursor->gp_used <= abi->gp_argument_count - 2U)
                cursor->gp_used += 2U;
            else { loc->on_stack = true; cursor->stack_slots += 2U; }
        } else {
            /* SysV AMD64 classifies any aggregate larger than 16 bytes as
             * MEMORY. Smaller unsupported SSE or mixed classes must remain
             * explicit failures. Keep emission bounded to avoid huge stubs. */
            size_t alignment = 0U;
            bool has_layout;
            bytes = 0U;
            {
                const MinicCoreType *resolved_record = minic_core_type_get(
                    fn->type_context,
                    minic_core_type_unqualified(fn->type_context, core_id));
                const MinicCoreType *resolved_layout = minic_core_type_get(
                    fn->type_context, core_id);
                has_layout = resolved_record != NULL &&
                    resolved_layout != NULL &&
                    resolved_record->kind == MINIC_CORE_TYPE_RECORD &&
                    resolved_record->value.record.is_complete &&
                    resolved_layout->has_layout;
                if (has_layout) {
                    bytes = resolved_layout->size;
                    alignment = resolved_layout->alignment;
                }
                if (has_layout && bytes == 0U &&
                    resolved_record->value.record.field_count == 0U) {
                    loc->kind = MINIC_X86_ARG_RECORD_EMPTY;
                    return true;
                }
            }
            if (!has_layout || bytes <= 16U || bytes > 4096U ||
                alignment == 0U || alignment > 16U) {
                if (getenv("MINIC_M1_TRACE") != NULL)
                    (void)fprintf(stderr,
                        "MINIC_X86_ARG_RECORD_CLASS_REJECT core_type=%zu "
                        "bytes=%zu align=%zu\n",
                        (size_t)core_id, bytes, alignment);
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
    } else if (type_class == X86_ABI_CLASS_FLOAT ||
               type_class == X86_ABI_CLASS_DOUBLE) {
        loc->kind = MINIC_X86_ARG_SSE;
        if (cursor->fp_used < abi->fp_argument_count) ++cursor->fp_used;
        else { loc->on_stack = true; ++cursor->stack_slots; }
    } else if (type_class == X86_ABI_CLASS_LONG_DOUBLE) {
        loc->kind = MINIC_X86_ARG_X87_MEMORY;
        cursor->stack_slots += cursor->stack_slots & 1U;
        loc->stack_index = cursor->stack_slots;
        loc->on_stack = true;
        cursor->stack_slots += 2U;
    } else if (type_class == X86_ABI_CLASS_INT128) {
        /* The scalar TI ABI must not be treated as a record or as one
         * eightbyte. Both INTEGER registers are consumed atomically.
         * Stack fallback is 16-byte aligned, unlike ordinary scalars. */
        loc->kind = MINIC_X86_ARG_I128_TWO_GP;
        if (cursor->gp_used <= abi->gp_argument_count - 2U) {
            cursor->gp_used += 2U;
        } else {
            loc->on_stack = true;
            cursor->stack_slots += cursor->stack_slots & 1U;
            loc->stack_index = cursor->stack_slots;
            cursor->stack_slots += 2U;
        }
    } else {
        if (!x86_plan_scalar_width(fn, core_id, &bytes, &sign))
            return false;
        loc->kind = MINIC_X86_ARG_INTEGER;
        if (cursor->gp_used < abi->gp_argument_count) ++cursor->gp_used;
        else { loc->on_stack = true; ++cursor->stack_slots; }
    }
    return cursor->stack_slots <= (size_t)INT32_MAX / 8U;
}

/* Microsoft x64 uses the same four *positional* slots for GP and SSE
 * arguments. Stack arguments start after the compulsory 32-byte home area;
 * carrying their source position as stack_index makes position four land at
 * 32(%rsp) on calls and at 48(%rbp) on function entry. */
static bool x86_ms_x64_place(const MinicCoreFunction *fn,
                            MinicCoreTypeId core_id,
                            size_t position, MinicX86ArgLocation *loc) {
    const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
        MINIC_CALLING_CONVENTION_MS_X64);
    size_t bytes;
    bool sign;
    const X86AbiCoreClass type_class = x86_plan_class(fn, core_id);
    if (abi == NULL || loc == NULL ||
        position > ((size_t)INT32_MAX - 16U) / 8U ||
        type_class == X86_ABI_CLASS_INVALID ||
        type_class == X86_ABI_CLASS_VOID ||
        type_class == X86_ABI_CLASS_RECORD ||
        type_class == X86_ABI_CLASS_INT128 ||
        type_class == X86_ABI_CLASS_LONG_DOUBLE) return false;
    (void)memset(loc, 0, sizeof(*loc));
    loc->gp_index = position;
    loc->fp_index = position;
    loc->stack_index = position;
    loc->on_stack = position >= abi->gp_argument_count;
    if (type_class == X86_ABI_CLASS_FLOAT ||
        type_class == X86_ABI_CLASS_DOUBLE) {
        loc->kind = MINIC_X86_ARG_SSE;
        return true;
    }
    if (!x86_plan_scalar_width(fn, core_id, &bytes, &sign))
        return false;
    loc->kind = MINIC_X86_ARG_INTEGER;
    return true;
}

static bool x86_ms_x64_scalar_return(const MinicCoreFunction *fn,
                                    MinicCoreTypeId core_id) {
    size_t bytes;
    bool sign;
    const X86AbiCoreClass cls = x86_plan_class(fn, core_id);
    return cls == X86_ABI_CLASS_VOID || cls == X86_ABI_CLASS_FLOAT ||
           cls == X86_ABI_CLASS_DOUBLE ||
           (cls == X86_ABI_CLASS_SCALAR &&
            x86_plan_scalar_width(fn, core_id, &bytes, &sign));
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
    const MinicCoreFunction *fn, MinicX86ArgumentPlan *plan) {
    MinicX86ArgCursor cursor = {0};
    size_t i;
    if (fn == NULL ||
        fn->type_context == NULL || plan == NULL ||
        !minic_function_calling_convention_valid(fn->calling_convention) ||
        (fn->parameter_count != 0U &&
         fn->core_parameter_type_ids == NULL) ||
        !x86_sysv_plan_alloc(fn->parameter_count, plan)) return false;
    plan->calling_convention = fn->calling_convention;
    if (fn->calling_convention == MINIC_CALLING_CONVENTION_MS_X64) {
        const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
            MINIC_CALLING_CONVENTION_MS_X64);
        if (abi == NULL) goto failed;
        if (!x86_ms_x64_scalar_return(fn, fn->core_return_type_id))
            goto failed;
        for (i = 0U; i < fn->parameter_count; ++i)
            if (!x86_ms_x64_place(fn, fn->core_parameter_type_ids[i], i,
                                  &plan->locations[i])) goto failed;
        if (fn->parameter_count > abi->gp_argument_count)
            cursor.stack_slots = fn->parameter_count - abi->gp_argument_count;
        plan->end = cursor;
        return true;
    }
    {
        size_t result_bytes;
        if (x86_plan_memory_result_record(fn, fn->core_return_type_id,
                                          &result_bytes))
            cursor.gp_used = 1U;
    }
    for (i = 0U; i < fn->parameter_count; ++i)
        if (!x86_sysv_place(fn, fn->core_parameter_type_ids[i], &cursor,
                            &plan->locations[i])) goto failed;
    plan->end = cursor;
    return true;
failed:
    minic_x86_64_argument_plan_destroy(plan);
    return false;
}

bool minic_x86_64_plan_outgoing_arguments(
    const MinicCoreFunction *fn, const MinicCoreCallView *view,
    MinicX86ArgumentPlan *plan) {
    MinicX86ArgCursor cursor = {0};
    size_t i, bytes;
    if (fn == NULL ||
        fn->type_context == NULL ||
        view == NULL || plan == NULL ||
        view->parameter_count > view->argument_count ||
        (view->argument_count != 0U && view->arguments == NULL) ||
        (view->parameter_count != 0U &&
         view->core_parameter_type_ids == NULL) ||
        !minic_function_calling_convention_valid(view->calling_convention) ||
        !x86_sysv_plan_alloc(view->argument_count, plan)) return false;
    plan->calling_convention = view->calling_convention;
    if (view->calling_convention == MINIC_CALLING_CONVENTION_MS_X64) {
        const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
            MINIC_CALLING_CONVENTION_MS_X64);
        /* Win64 consumes one slot per source parameter: GP and XMM registers
         * have the SAME four positional indices, unlike SysV independent
         * integer/FP cursors. Every call, including zero-arg, reserves 32B
         * home space. Slots 4+ occupy the stack above that home space. */
        if (abi == NULL || view->is_variadic ||
            !x86_ms_x64_scalar_return(fn, view->core_return_type_id) ||
            (view->argument_count > ((size_t)INT32_MAX - 47U) / 8U))
            goto failed;
        plan->is_variadic = false;
        for (i = 0U; i < view->argument_count; ++i) {
            MinicCoreTypeId core_type = MINIC_CORE_TYPE_INVALID;
            if (!minic_core_call_view_argument_core_type(
                    fn, view, i, &core_type))
                goto failed;
            if (i < view->parameter_count) {
                core_type = view->core_parameter_type_ids[i];
            }
            if (!x86_ms_x64_place(fn, core_type, i, &plan->locations[i]))
                goto failed;
        }
        if (view->argument_count > abi->gp_argument_count)
            cursor.stack_slots = view->argument_count - abi->gp_argument_count;
        bytes = 32U + cursor.stack_slots * 8U;
        plan->stack_bytes = (bytes + 15U) & ~(size_t)15U;
        plan->end = cursor;
        return true;
    }
    {
        size_t result_bytes;
        if (view->return_form == MINIC_CORE_CALL_RETURN_OBJECT &&
            x86_plan_memory_result_record(fn, view->core_return_type_id,
                                          &result_bytes))
            cursor.gp_used = 1U;
    }
    plan->is_variadic = view->is_variadic;
    for (i = 0U; i < view->argument_count; ++i) {
        MinicCoreTypeId core_type = MINIC_CORE_TYPE_INVALID;
        if (!minic_core_call_view_argument_core_type(
                fn, view, i, &core_type))
            goto failed;
        /* SysV placement intentionally uses the fixed declared type.
         * Shared Core checks already enforce aggregate identity. */
        if (i < view->parameter_count) {
            core_type = view->core_parameter_type_ids[i];
        }
        if (!x86_sysv_place(fn, core_type, &cursor, &plan->locations[i]))
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
    const MinicCoreFunction *fn, const MinicCoreCallIndex *calls,
    MinicX86OutgoingCallPlans *result) {
    size_t i, previous = SIZE_MAX;
    if (fn == NULL || calls == NULL ||
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
                fn, &calls->sites[i].view,
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
