#include "target/x86_64/call_plan.h"
#include "target/x86_64/abi.h"
#include "target/data_layout.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
            size_t alignment;
            if (!minic_data_layout_type(target->data_layout, program, type,
                                        &bytes, &alignment) ||
                bytes <= 16U || bytes > 4096U ||
                alignment == 0U || alignment > 16U)
                return false;
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
        (fn->parameter_count != 0U && fn->parameter_types == NULL) ||
        !x86_sysv_plan_alloc(fn->parameter_count, plan)) return false;
    for (i = 0U; i < fn->parameter_count; ++i)
        if (!x86_sysv_place(program, target, fn->parameter_types[i],
                             &cursor, &plan->locations[i])) {
            minic_x86_64_argument_plan_destroy(plan);
            return false;
        }
    plan->end = cursor;
    return true;
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
        !x86_sysv_plan_alloc(view->argument_count, plan)) return false;
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
