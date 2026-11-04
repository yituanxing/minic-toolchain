#include "target/riscv64/function_abi_plan.h"
#include "target/riscv64/core_type_query.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool rv64_fn_is_record(const MinicCoreFunction *fn,
                             MinicCoreTypeId id) {
    return minic_riscv64_core_is_record(fn, id);
}

static bool rv64_fn_is_fp_scalar(const MinicCoreFunction *fn,
                                MinicCoreTypeId id) {
    const MinicCoreType *type = minic_riscv64_core_base_type(fn, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_FLOAT &&
           (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 ||
            type->value.floating.format == MINIC_CORE_FLOAT_BINARY64);
}

static bool rv64_fn_is_wide_scalar(const MinicCoreFunction *fn,
                                  MinicCoreTypeId id) {
    const MinicCoreType *type = minic_riscv64_core_base_type(fn, id);
    return type != NULL &&
           ((type->kind == MINIC_CORE_TYPE_FLOAT &&
             type->value.floating.format == MINIC_CORE_FLOAT_EXTENDED) ||
            (type->kind == MINIC_CORE_TYPE_INTEGER &&
             type->value.integer.bit_width == 128U));
}
/* Return and named-argument legality have exactly one psABI owner.
 * The Core verifier proves types and identities; this checked placement
 * plan proves RV64 register/stack forms before admission or emission.
 * Do not repeat its classification in core_admission.c. */
static bool minic_riscv64_function_abi_plan_qualified(
    const MinicCoreFunction *function,
    const MinicRiscv64FunctionAbiPlan *abi_plan) {
    size_t index;
        MinicRiscv64AbiValue return_value;
        if (function == NULL || function->type_context == NULL ||
            (minic_core_function_parameter_count(function) != 0U &&
             minic_core_function_parameter_types(function) == NULL) ||
            abi_plan == NULL || !abi_plan->prepared ||
            abi_plan->parameter_location_count != minic_core_function_parameter_count(function)) {
            return false;
        }
        return_value = abi_plan->return_value;
        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INVALID) {
            return false;
        }
        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
            if (return_value.slot_count == 0U || return_value.slot_count > 2U) {
                return false;
            }
        } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            if (!rv64_fn_is_record(function, minic_core_function_return_type(function)) ||
                return_value.storage_size <= 16U || return_value.slot_count != 1U) {
                return false;
            }
        } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR) {
            if (!rv64_fn_is_wide_scalar(function, minic_core_function_return_type(function)) ||
                return_value.storage_size != 16U || return_value.slot_count != 2U) {
                return false;
            }
        } else if (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
                   return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                   !(return_value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&
                     rv64_fn_is_fp_scalar(function, minic_core_function_return_type(function)))) {
            return false;
        }
        for (index = 0U; index < minic_core_function_parameter_count(function); ++index) {
            const MinicRiscv64AbiArgumentLocation location =
                abi_plan->parameter_locations[index];
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (!rv64_fn_is_record(function,
                        minic_core_function_parameter_types(function)[index]) ||
                    location.value.storage_size == 0U || location.value.slot_count != 1U ||
                    location.floating_register_count != 0U ||
                    !((location.integer_register_count == 1U &&
                       location.integer_register_begin < 8U &&
                       location.stack_slot_count == 0U) ||
                      (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U))) {
                    return false;
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT) {
                /* LP64D spills named floating parameters into the normal
                 * integer argument stream after fa7. LP64 never uses FARs. */
                bool correct_location =
                    location.floating_register_count == 1U
                        ? (!abi_plan->named_parameter_cursor.lp64_soft_float &&
                           location.floating_register_begin < 8U &&
                           location.integer_register_count == 0U &&
                           location.stack_slot_count == 0U)
                        : (location.floating_register_count == 0U &&
                           ((location.integer_register_count == 1U &&
                             location.integer_register_begin < 8U &&
                             location.stack_slot_count == 0U) ||
                            (location.integer_register_count == 0U &&
                             location.stack_slot_count == 1U)));
                if (!rv64_fn_is_fp_scalar(function,
                        minic_core_function_parameter_types(function)[index]) ||
                    !correct_location) {
                    return false;
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR) {
                if (!rv64_fn_is_wide_scalar(function,
                        minic_core_function_parameter_types(function)[index]) ||
                    location.value.storage_size != 16U || location.value.slot_count != 2U ||
                    location.floating_register_count != 0U ||
                    location.integer_register_count + location.stack_slot_count != 2U ||
                    location.integer_register_begin + location.integer_register_count > 8U) {
                    return false;
                }
            } else if (location.value.kind != MINIC_RISCV64_ABI_VALUE_IGNORE &&
                       location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                       (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                        location.value.slot_count == 0U || location.value.slot_count > 2U)) {
                return false;
            }
        }
    return true;
}

/* Return-pointer reservation, float/int register cursors and split stack
 * locations are classified once and subsequently passed into the frame. */
bool minic_riscv64_function_abi_plan_build_profile(
    const MinicCoreFunction *fn,
    MinicRiscv64FunctionAbiPlan *plan, bool lp64_soft_float) {
    MinicRiscv64AbiCursor cursor;
    size_t i;
    if (fn == NULL || fn->type_context == NULL || plan == NULL ||
        plan->prepared || plan->parameter_locations != NULL ||
        (minic_core_function_parameter_count(fn) != 0U && minic_core_function_parameter_types(fn) == NULL) ||
        minic_core_function_parameter_count(fn) > SIZE_MAX / sizeof(*plan->parameter_locations) ||
        !minic_riscv64_abi_cursor_initialize_for_core_return_profile(
            fn->type_context, minic_core_function_return_type(fn), lp64_soft_float,
            &cursor, &plan->return_value))
        return false;
    if (minic_core_function_parameter_count(fn) != 0U) {
        plan->parameter_locations = (MinicRiscv64AbiArgumentLocation *)calloc(
            minic_core_function_parameter_count(fn), sizeof(*plan->parameter_locations));
        if (plan->parameter_locations == NULL) return false;
    }
    for (i = 0U; i < minic_core_function_parameter_count(fn); ++i) {
        if (!minic_riscv64_abi_place_core_argument(
                fn->type_context, minic_core_function_parameter_types(fn)[i], true,
                &cursor, &plan->parameter_locations[i])) {
            minic_riscv64_function_abi_plan_destroy(plan);
            return false;
        }
    }
    plan->named_parameter_cursor = cursor;
    plan->parameter_location_count = minic_core_function_parameter_count(fn);
    plan->prepared = true;
    if (!minic_riscv64_function_abi_plan_qualified(fn, plan)) {
        minic_riscv64_function_abi_plan_destroy(plan);
        return false;
    }
    return true;
}
bool minic_riscv64_function_abi_plan_build(
    const MinicCoreFunction *fn,
    MinicRiscv64FunctionAbiPlan *plan) {
    return minic_riscv64_function_abi_plan_build_profile(fn, plan, false);
}
void minic_riscv64_function_abi_plan_destroy(
    MinicRiscv64FunctionAbiPlan *plan) {
    size_t i;
    if (plan == NULL) return;
    free(plan->parameter_locations);
    /* ABI unit tests link the function planner without machine-call cache
     * implementation; release ownership locally without another link edge. */
    if (plan->outgoing_calls.sites != NULL)
        for (i = 0U; i < plan->outgoing_calls.count; ++i)
            free(plan->outgoing_calls.sites[i]);
    free(plan->outgoing_calls.sites);
    (void)memset(plan, 0, sizeof(*plan));
}
