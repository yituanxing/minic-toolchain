#include "target/common/direct_call_plan.h"
#include "target/common/core_call_view.h"
#include "target/common/core_instruction_family.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/lp64_scalar.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Locations are ABI-owned; no architecture-specific instructions here. */
static bool minic_lp64_plan_types(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicType *types, size_t count, size_t fixed, bool variadic,
    MinicLP64DirectABI abi, MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64ArgumentPlan *plan) {
    size_t gp = 0U, fp = 0U, stack = 0U, j;
    if (program == NULL || target == NULL || plan == NULL ||
        indirect_record == NULL || count > MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS ||
        (count != 0U && types == NULL) || fixed > count ||
        (!variadic && fixed != count) ||
        (abi != MINIC_LP64_ABI_AAPCS64 &&
         abi != MINIC_LP64_ABI_LOONGARCH64_LP64D))
        return false;
    (void)memset(plan, 0, sizeof(*plan));
    plan->argument_count = count;
    for (j = 0U; j < count; ++j) {
        MinicLP64ArgumentLocation *loc = &plan->arguments[j];
        size_t chunks = 1U, size = 0U;
        const MinicType type = types[j];
        const bool record = minic_type_is_record(type);
        bool by_reference = false;
        loc->gp_first = loc->fp_first = loc->stack_first = SIZE_MAX;
        if (record) {
            if (!minic_multi_gp_record(program, target, type, &size, &chunks)) {
                if (!indirect_record(program, target, type, &size)) return false;
                by_reference = true;
                chunks = 1U;
            }
        }
        loc->size_bytes = size;
        loc->chunks = chunks;
        if (record) {
            if (by_reference) {
                loc->kind = MINIC_LP64_ARGUMENT_INDIRECT_RECORD;
                if (gp < 8U) {
                    loc->gp_first = gp;
                    loc->gp_registers = 1U;
                } else {
                    loc->stack_first = stack++;
                    loc->stack_slots = 1U;
                }
                ++gp;
            } else {
                size_t available = gp < 8U ? 8U - gp : 0U;
                const size_t used = abi == MINIC_LP64_ABI_AAPCS64
                    ? (available >= chunks ? chunks : 0U)
                    : (available < chunks ? available : chunks);
                loc->kind = MINIC_LP64_ARGUMENT_RECORD;
                if (used != 0U) {
                    loc->gp_first = gp;
                    loc->gp_registers = used;
                }
                if (used < chunks) {
                    loc->stack_first = stack;
                    loc->stack_slots = chunks - used;
                    stack += chunks - used;
                }
                gp = abi == MINIC_LP64_ABI_AAPCS64 && used == 0U
                    ? 8U : gp + used;
            }
        } else if (minic_type_is_int128_integer(type)) {
            /* TI is an integral 16-byte scalar, not a single GP word.
             * AAPCS64 C.10/C.11 mandates even GPR pairs and whole aligned
             * stack spills; LoongArch LP64D can split the final GPR pair. */
            loc->kind = MINIC_LP64_ARGUMENT_WIDE;
            loc->size_bytes = 16U;
            loc->chunks = 2U;
            if (abi == MINIC_LP64_ABI_AAPCS64) {
                gp = (gp + 1U) & ~(size_t)1U;
                if (gp <= 6U) {
                    loc->gp_first = gp;
                    loc->gp_registers = 2U;
                    gp += 2U;
                } else {
                    gp = 8U;
                    stack += stack & 1U;
                    loc->stack_first = stack;
                    loc->stack_slots = 2U;
                    stack += 2U;
                }
            } else {
                const size_t available = gp < 8U ? 8U - gp : 0U;
                const size_t used = available < 2U ? available : 2U;
                if (used != 0U) {
                    loc->gp_first = gp;
                    loc->gp_registers = used;
                } else stack += stack & 1U;
                if (used < 2U) {
                    loc->stack_first = stack;
                    loc->stack_slots = 2U - used;
                    stack += 2U - used;
                }
                gp += used;
            }
        } else if (minic_type_is_long_double(type)) {
            loc->kind = MINIC_LP64_ARGUMENT_WIDE;
            if (abi == MINIC_LP64_ABI_AAPCS64) {
                if (fp < 8U) {
                    loc->fp_first = fp;
                    loc->fp_registers = 1U;
                } else {
                    stack += stack & 1U;
                    loc->stack_first = stack;
                    loc->stack_slots = 2U;
                    stack += 2U;
                }
                ++fp;
            } else {
                size_t available, used;
                if (variadic && j >= fixed && gp < 8U && (gp & 1U)) ++gp;
                available = gp < 8U ? 8U - gp : 0U;
                used = available < 2U ? available : 2U;
                if (used != 0U) {
                    loc->gp_first = gp;
                    loc->gp_registers = used;
                } else stack += stack & 1U;
                if (used < 2U) {
                    loc->stack_first = stack;
                    loc->stack_slots = 2U - used;
                    stack += 2U - used;
                }
                gp += used;
            }
        } else if (minic_type_is_double(type) || minic_type_is_float(type)) {
            const bool use_fp = abi == MINIC_LP64_ABI_AAPCS64 ||
                ((!variadic || j < fixed) && fp < 8U);
            if (use_fp) {
                loc->kind = MINIC_LP64_ARGUMENT_FP;
                if (fp < 8U) {
                    loc->fp_first = fp;
                    loc->fp_registers = 1U;
                } else {
                    loc->stack_first = stack++;
                    loc->stack_slots = 1U;
                }
                ++fp;
            } else {
                loc->kind = MINIC_LP64_ARGUMENT_GP;
                if (gp < 8U) {
                    loc->gp_first = gp;
                    loc->gp_registers = 1U;
                } else {
                    loc->stack_first = stack++;
                    loc->stack_slots = 1U;
                }
                ++gp;
            }
        } else {
            loc->kind = MINIC_LP64_ARGUMENT_GP;
            if (gp < 8U) {
                loc->gp_first = gp;
                loc->gp_registers = 1U;
            } else {
                loc->stack_first = stack++;
                loc->stack_slots = 1U;
            }
            ++gp;
        }
    }
    /* Every call is already capped at 64 arguments, each at most two slots. */
    if (stack > (SIZE_MAX - 15U) / 8U) return false;
    plan->stack_slots = stack;
    plan->stack_bytes = (stack * 8U + 15U) & ~(size_t)15U;
    return true;
}

/* The same ABI classifier/placer serves outgoing CALL arguments and the
 * incoming named-parameter signature. ISA-specific value movement stays in
 * codegen; location assignment is performed exactly once per boundary. */
/* One-time per-function ingress planning. All function parameters are named
 * and therefore fixed even when the function declares an ellipsis.
 * Reuses exactly the same register/stack classifier as outgoing calls. */
bool minic_lp64_plan_incoming_parameters(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicLP64DirectABI abi,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64ArgumentPlan *plan) {
    if (fn == NULL) return false;
    return minic_lp64_plan_types(program, target, fn->parameter_types,
        fn->parameter_count, fn->parameter_count, false, abi,
        indirect_record, plan);
}

/* Shared CALL ABI admission and placement are one function-site pass. */
static bool minic_lp64_admit_call(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicCoreCallView *semantic_view,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicDirectLP64IndirectRecord indirect_return,
    const char *arch, const char *name, size_t i,
    MinicType types[MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS]) {
    MinicCoreCallView call;
    const MinicType *parameter_types;
    MinicType return_type;
    size_t j, begin, count, parameter_count;
    bool is_variadic;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        semantic_view == NULL || indirect_record == NULL || arch == NULL ||
        name == NULL || types == NULL)
        return false;
    call = *semantic_view;
        /* Core record results always use a result_object, not scalar SSA.
         * Small integer and FP forms use the established ABI registers;
         * larger indirect results require the separately supplied target
         * return callback. NULL keeps the old fail-closed behavior. */
        bool record_result = false;
        size_t return_record_bytes = 0U, return_record_slots = 0U;
        size_t fp_elements = 0U, fp_width = 0U;
        if (ins->kind == MINIC_CORE_INSTRUCTION_CALL &&
            minic_type_is_record(ins->type) &&
            ins->value.call.result_object < fn->object_count &&
            ins->result == MINIC_CORE_VALUE_INVALID &&
            fn->objects[ins->value.call.result_object].element_count == 1U &&
            minic_type_equal(fn->objects[ins->value.call.result_object].type,
                             ins->type) &&
            ((minic_multi_gp_record(program, target, ins->type,
                                   &return_record_bytes, &return_record_slots) &&
              return_record_bytes > 0U && return_record_bytes <= 16U &&
              return_record_slots >= 1U && return_record_slots <= 2U) ||
             minic_multi_fp_return_record(program, target, ins->type,
                                           &fp_elements, &fp_width) ||
             (indirect_return != NULL &&
              indirect_return(program, target, ins->type,
                              &return_record_bytes) && return_record_bytes > 16U)))
            record_result = true;
        if (!record_result &&
            ((minic_type_is_void(ins->type)
             ? ins->result != MINIC_CORE_VALUE_INVALID
             : ins->result == MINIC_CORE_VALUE_INVALID ||
               ins->result >= fn->value_count ||
               !minic_type_equal(fn->values[ins->result].type, ins->type)) ||
            !minic_lp64_valid_return_with_indirect(
                program, target, ins->type, indirect_return))) {
            if (getenv("MINIC_BACKEND_CENSUS") != NULL)
                (void)fprintf(stderr,
                    "MINIC_CALL_RESULT_REJECT arch=%s function=%s "
                    "instruction=%zu return_base=%d is_float128=%d "
                    "return_valid=%d value_match=%d\n",
                    arch, name, i, (int)ins->type.base_kind,
                    minic_type_is_float128(ins->type) ? 1 : 0,
                    minic_lp64_valid_return_with_indirect(
                        program, target, ins->type, indirect_return) ? 1 : 0,
                    ins->result < fn->value_count &&
                    minic_type_equal(fn->values[ins->result].type, ins->type) ? 1 : 0);
            return false;
        }
        /* Core/ABI-neutral return shape was already checked by the view. */
        if (record_result &&
            call.return_form != MINIC_CORE_CALL_RETURN_OBJECT)
            return false;
        if (call.callee != NULL &&
            ((ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID &&
              !record_result) ||
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
                          minic_lp64_valid_return_with_indirect(
                              program, target, return_type, indirect_return) ? 1 : 0);
        if (!minic_type_equal(return_type, ins->type) ||
            /* Fixed binary64 call results are transferred from the target FP
             * ABI result register; aggregate and other types still fail closed. */
            (!record_result &&
             !minic_lp64_valid_return_with_indirect(
                 program, target, return_type, indirect_return)) ||
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
            if (!minic_core_call_view_argument_type(fn, &call, j, &actual))
                return false;
            types[j] = actual;
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
            if (!minic_lp64_valid_parameter_type(program, target, actual))
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
    return true;
}

/* One physical call-plan entry per actual Core CALL. The prepared Common
 * call index supplies only real, ordered CALL sites. This cache is shared by
 * AArch64 AAPCS64 and LoongArch64 LP64D; only their ABI classifier differs.
 * Standalone callers without a Common index still use a checked linear
 * fallback, but production work is O(CALL sites), not O(all instructions). */
bool minic_lp64_plan_function_calls(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreCallIndex *semantic_calls,
    MinicLP64DirectABI abi, MinicDirectLP64IndirectRecord indirect_record,
    MinicDirectLP64IndirectRecord indirect_return,
    const char *arch, const char *name, size_t *failed_instruction,
    bool *failed_preflight, bool *has_calls, MinicLP64CallPlanCache *cache) {
    const MinicCoreCallIndex *calls;
    size_t ordinal, previous_id = SIZE_MAX;
    if (failed_instruction != NULL) *failed_instruction = SIZE_MAX;
    if (failed_preflight != NULL) *failed_preflight = true;
    if (has_calls != NULL) *has_calls = false;
    if (program == NULL || target == NULL || fn == NULL || cache == NULL ||
        arch == NULL || name == NULL || failed_instruction == NULL ||
        failed_preflight == NULL || has_calls == NULL ||
        cache->sites != NULL || cache->call_count != 0U ||
        cache->semantic_calls.index != NULL ||
        indirect_record == NULL ||
        !minic_core_call_index_handle_prepare(
            fn, semantic_calls, &cache->semantic_calls))
        return false;
    calls = cache->semantic_calls.index;
    if (calls->count > SIZE_MAX / sizeof(*cache->sites))
        goto failed;
    *has_calls = calls->count != 0U;
    if (calls->count == 0U) {
        *failed_preflight = false;
        return true;
    }
    *failed_preflight = false;
    cache->sites = calloc(calls->count, sizeof(*cache->sites));
    if (cache->sites == NULL) goto failed;
    cache->call_count = calls->count;
    for (ordinal = 0U; ordinal < calls->count; ++ordinal) {
        const size_t id = calls->sites[ordinal].instruction_id;
        MinicType types[MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS];
        MinicLP64ArgumentPlan scratch;
        MinicLP64PreparedCall *site;
        size_t bytes;
        *failed_instruction = id;
        *failed_preflight = true;
        if (id >= fn->instruction_count ||
            (previous_id != SIZE_MAX && id <= previous_id) ||
            !minic_core_instruction_is_call(fn->instructions[id].kind) ||
            !minic_lp64_admit_call(
                program, target, fn, &fn->instructions[id],
                &calls->sites[ordinal].view, indirect_record, indirect_return,
                arch, name,
                id, types))
            goto failed;
        *failed_preflight = false;
        if (!minic_lp64_plan_types(program, target, types,
                                  calls->sites[ordinal].view.argument_count,
                                  calls->sites[ordinal].view.parameter_count,
                                  calls->sites[ordinal].view.is_variadic,
                                  abi, indirect_record, &scratch) ||
            scratch.argument_count >
                (SIZE_MAX - sizeof(*site)) / sizeof(*site->arguments))
            goto failed;
        bytes = sizeof(*site) +
            scratch.argument_count * sizeof(*site->arguments);
        site = malloc(bytes);
        if (site == NULL) goto failed;
        site->instruction_id = id;
        site->argument_count = scratch.argument_count;
        site->stack_bytes = scratch.stack_bytes;
        if (scratch.argument_count != 0U)
            memcpy(site->arguments, scratch.arguments,
                   scratch.argument_count * sizeof(*site->arguments));
        cache->sites[ordinal] = site;
        previous_id = id;
    }
    *failed_instruction = SIZE_MAX;
    *failed_preflight = false;
    return true;
failed:
    minic_lp64_call_plan_cache_destroy(cache);
    return false;
}

const MinicLP64PreparedCall *minic_lp64_function_call_plan(
    const MinicLP64CallPlanCache *cache, size_t instruction_id) {
    const MinicCoreCallIndex *calls;
    size_t ordinal;
    if (cache == NULL || cache->semantic_calls.index == NULL ||
        (cache->call_count != 0U && cache->sites == NULL))
        return NULL;
    calls = cache->semantic_calls.index;
    if (calls->count != cache->call_count ||
        minic_core_call_index_find(calls, instruction_id, &ordinal) == NULL ||
        ordinal >= cache->call_count || cache->sites[ordinal] == NULL ||
        cache->sites[ordinal]->instruction_id != instruction_id)
        return NULL;
    return cache->sites[ordinal];
}

void minic_lp64_call_plan_cache_destroy(MinicLP64CallPlanCache *cache) {
    size_t i;
    if (cache == NULL) return;
    for (i = 0U; i < cache->call_count; ++i)
        free(cache->sites[i]);
    free(cache->sites);
    minic_core_call_index_handle_destroy(&cache->semantic_calls);
    (void)memset(cache, 0, sizeof(*cache));
}
