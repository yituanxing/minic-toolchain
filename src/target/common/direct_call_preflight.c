#include "target/common/direct_call_preflight.h"
#include "target/common/core_call_view.h"
#include "target/common/core_instruction_family.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/lp64_scalar.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Shared call semantics and admission, not a universal target ABI.
 * Register/stack assignment and ISA emission are still target-owned. */
bool minic_multi_direct_lp64_calls_preflight(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const char *arch, const char *name,
    MinicDirectLP64IndirectRecord indirect_record,
    size_t *failed_instruction, bool *has_calls) {
    size_t i;
    if (program == NULL || target == NULL || fn == NULL || arch == NULL ||
        name == NULL || indirect_record == NULL ||
        failed_instruction == NULL || has_calls == NULL) return false;
    *failed_instruction = SIZE_MAX;
    *has_calls = false;
    for (i = 0U; i < fn->instruction_count; ++i) {
        *failed_instruction = i;
        const MinicCoreInstruction *ins = &fn->instructions[i];
        MinicCoreCallView call;
        const MinicType *parameter_types;
        MinicType return_type;
        size_t j, begin, count, parameter_count;
        bool is_variadic;
        if (!minic_core_instruction_is_call(ins->kind)) continue;
        *has_calls = true; /* Caller preserves the return-address register. */
        /* GNU div_t/ldiv_t and similar small integer-only aggregate
         * results travel in one or two GP registers. Core represents their
         * destination as a result_object, NOT a scalar SSA result value.
         * Keep large/FP-class/indirect aggregate returns unsupported. */
        bool small_record_result = false;
        size_t return_record_bytes = 0U, return_record_slots = 0U;
        if (ins->kind == MINIC_CORE_INSTRUCTION_CALL &&
            minic_type_is_record(ins->type) &&
            ins->value.call.result_object < fn->object_count &&
            ins->result == MINIC_CORE_VALUE_INVALID &&
            fn->objects[ins->value.call.result_object].element_count == 1U &&
            minic_type_equal(fn->objects[ins->value.call.result_object].type,
                             ins->type) &&
            minic_multi_gp_record(program, target, ins->type,
                                  &return_record_bytes, &return_record_slots) &&
            return_record_bytes > 0U && return_record_bytes <= 16U &&
            return_record_slots >= 1U && return_record_slots <= 2U)
            small_record_result = true;
        if (!small_record_result &&
            ((minic_type_is_void(ins->type)
             ? ins->result != MINIC_CORE_VALUE_INVALID
             : ins->result == MINIC_CORE_VALUE_INVALID ||
               ins->result >= fn->value_count ||
               !minic_type_equal(fn->values[ins->result].type, ins->type)) ||
            !minic_lp64_valid_return_type(program, target, ins->type))) {
            if (getenv("MINIC_BACKEND_CENSUS") != NULL)
                (void)fprintf(stderr,
                    "MINIC_CALL_RESULT_REJECT arch=%s function=%s "
                    "instruction=%zu return_base=%d is_float128=%d "
                    "return_valid=%d value_match=%d\n",
                    arch, name, i, (int)ins->type.base_kind,
                    minic_type_is_float128(ins->type) ? 1 : 0,
                    minic_lp64_valid_return_type(program, target, ins->type) ? 1 : 0,
                    ins->result < fn->value_count &&
                    minic_type_equal(fn->values[ins->result].type, ins->type) ? 1 : 0);
            return false;
        }
        if (!minic_core_call_view_build(fn, ins, &call))
            return false;
        /* Core/ABI-neutral return shape was already checked by the view. */
        if (small_record_result &&
            call.return_form != MINIC_CORE_CALL_RETURN_OBJECT)
            return false;
        if (call.callee != NULL &&
            ((ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID &&
              !small_record_result) ||
             call.callee->name == NULL || call.callee->name[0] == '\0'))
            return false;
        begin = call.argument_begin;
        count = call.argument_count;
        return_type = call.return_type;
        parameter_types = call.parameter_types;
        parameter_count = call.parameter_count;
        is_variadic = call.is_variadic;
        /* Batch Lua triage: expose actual ABI gate dimensions rather than
         * spending a runner cycle on each failed translation unit. */
        if (getenv("MINIC_BACKEND_CENSUS") != NULL && count > 8U)
            (void)fprintf(stderr,
                          "MINIC_BACKEND_CALL_GATE arch=%s function=%s "
                          "instruction=%zu count=%zu fixed=%zu variadic=%d "
                          "indirect=%d return_valid=%d\n",
                          arch, name, i, count, parameter_count,
                          is_variadic ? 1 : 0,
                          ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL ? 1 : 0,
                          minic_lp64_valid_return_type(program, target, return_type) ? 1 : 0);
        if (!minic_type_equal(return_type, ins->type) ||
            /* Fixed binary64 call results are transferred from the target FP
             * ABI result register; aggregate and other types still fail closed. */
            (!small_record_result &&
             !minic_lp64_valid_return_type(program, target, return_type)) ||
            /* Original BusyBox ascii has a 24-argument printf. The existing
             * AAPCS64/LP64D call emitter already classifies outgoing GP/FP
             * and spill arguments by cursor, without fixed 16-element
             * arrays. Admit bounded larger calls, keeping frame/argument
             * index overflow checks fail-closed. */
            count > MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS || parameter_count > count ||
            (!is_variadic && parameter_count != count) ||
            begin > fn->call_argument_count ||
            count > fn->call_argument_count - begin ||
            (parameter_count != 0U && parameter_types == NULL))
            return false;
        for (j = 0U; j < count; ++j) {
            const MinicCoreCallArgument *arg = &call.arguments[j];
            MinicType actual;
            size_t actual_width;
            bool actual_signed;
            if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                const MinicCoreObjectId object_id = arg->value.object_id;
                size_t record_size, record_slots;
                /* Original BusyBox ipcrm/ipcs pass union semun by
                 * value to variadic semctl(). A single integer-class
                 * eightbyte uses the ordinary GP/overflow integer ABI,
                 * including when the argument is beyond the fixed list.
                 * Larger variadic aggregates and FP/HFA records remain
                 * explicitly unsupported rather than guessed. */
                if (object_id >= fn->object_count ||
                    fn->objects[object_id].element_count != 1U ||
                    (j < parameter_count &&
                     !minic_type_equal(fn->objects[object_id].type,
                                       parameter_types[j])) ||
                    (j >= parameter_count && !is_variadic) ||
                    (!minic_multi_gp_record(program, target,
                                            fn->objects[object_id].type,
                                            &record_size, &record_slots) &&
                     (j >= parameter_count ||
                      !indirect_record(program, target,
                             fn->objects[object_id].type, &record_size))) ||
                    (j >= parameter_count &&
                     (record_size > 8U || record_slots != 1U)))
                    return false;
                continue;
            }
            if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                arg->value.value_id >= fn->value_count) {
                if (getenv("MINIC_BACKEND_CENSUS") != NULL)
                    (void)fprintf(stderr,
                        "MINIC_SQLITE_CALL_ARGUMENT arch=%s function=%s argument=%zu arg_kind=%d fixed=%zu total=%zu expected_base=%d\n",
                        arch, name, j, (int)arg->kind, parameter_count, count,
                        j < parameter_count ? (int)parameter_types[j].base_kind : -1);
                return false;
            }
            if (!minic_core_call_view_argument_type(fn, &call, j, &actual) ||
                !minic_lp64_valid_parameter_type(program, target, actual))
                return false;
            if (j < parameter_count) {
                if (!minic_lp64_valid_parameter_type(program, target, parameter_types[j]) ||
                    !minic_type_equal(parameter_types[j], actual))
                    return false;
            } else {
                /* Lua source uses size_t/long-long variadic arguments.
                 * Preserve 32-bit promotions and classify full LP64
                 * integer values in the native general-purpose registers. */
                if (!is_variadic ||
                    (!minic_type_is_long_double(actual) &&
                     !minic_type_is_double(actual) &&
                     !minic_type_is_pointer(actual) &&
                     (!minic_lp64_scalar_integer_width(program, target, actual,
                                            &actual_width, &actual_signed) ||
                      (actual_width != 4U && actual_width != 8U)))) return false;
            }
        }
    }
    return true;
}
