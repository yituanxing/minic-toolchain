#include "target/common/direct_call_plan.h"
#include "target/common/core_call_view.h"
#include "target/common/core_instruction_family.h"
#include "target/common/lp64_aggregate.h"
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
bool minic_lp64_plan_direct_call(const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicCoreInstruction *call, const MinicCoreCallView *semantic_view,
    MinicLP64DirectABI abi,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64ArgumentPlan *plan) {
    MinicType types[MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS];
    MinicCoreCallView view;
    size_t count, j;
    if (!(semantic_view != NULL ? (view = *semantic_view, true) :
          minic_core_call_view_build(fn, call, &view)) ||
        view.argument_count > MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS)
        return false;
    count = view.argument_count;
    for (j = 0U; j < count; ++j) {
        if (!minic_core_call_view_argument_type(fn, &view, j, &types[j]))
            return false;
    }
    return minic_lp64_plan_types(program, target, types, count,
                                view.parameter_count, view.is_variadic,
                                abi, indirect_record, plan);
}

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

/* One physical call-plan entry per actual Core CALL. The prepared Common
 * call index supplies only real, ordered CALL sites. This cache is shared by
 * AArch64 AAPCS64 and LoongArch64 LP64D; only their ABI classifier differs.
 * Standalone callers without a Common index still use a checked linear
 * fallback, but production work is O(CALL sites), not O(all instructions). */
bool minic_lp64_plan_function_calls(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreCallIndex *semantic_calls,
    MinicLP64DirectABI abi, MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64CallPlanCache *cache) {
    size_t ordinal, id = 0U, calls = 0U, previous_id = SIZE_MAX;
    if (program == NULL || target == NULL || fn == NULL || cache == NULL ||
        cache->sites != NULL || cache->call_count != 0U ||
        indirect_record == NULL ||
        (fn->instruction_count != 0U && fn->instructions == NULL))
        return false;
    if (semantic_calls != NULL) {
        if (semantic_calls->count > fn->instruction_count ||
            (semantic_calls->count != 0U && semantic_calls->sites == NULL))
            return false;
        calls = semantic_calls->count;
    } else {
        for (id = 0U; id < fn->instruction_count; ++id)
            if (minic_core_instruction_is_call(fn->instructions[id].kind))
                ++calls;
    }
    if (calls > SIZE_MAX / sizeof(*cache->sites))
        return false;
    if (calls == 0U) return true;
    cache->sites = (MinicLP64PreparedCall **)calloc(calls, sizeof(*cache->sites));
    if (cache->sites == NULL) return false;
    cache->call_count = calls;
    id = 0U;
    for (ordinal = 0U; ordinal < calls; ++ordinal) {
        const MinicCoreInstruction *ins;
        const MinicCoreCallView *view = NULL;
        MinicLP64ArgumentPlan scratch;
        MinicLP64PreparedCall *site;
        size_t bytes;
        if (semantic_calls != NULL) {
            id = semantic_calls->sites[ordinal].instruction_id;
            view = &semantic_calls->sites[ordinal].view;
        } else {
            while (id < fn->instruction_count &&
                   !minic_core_instruction_is_call(fn->instructions[id].kind))
                ++id;
        }
        if (id >= fn->instruction_count ||
            (previous_id != SIZE_MAX && id <= previous_id))
            goto failed;
        ins = &fn->instructions[id];
        if (!minic_core_instruction_is_call(ins->kind) ||
            !minic_lp64_plan_direct_call(
                program, target, fn, ins, view, abi, indirect_record, &scratch) ||
            scratch.argument_count >
                (SIZE_MAX - sizeof(*site)) / sizeof(*site->arguments))
            goto failed;
        bytes = sizeof(*site) +
            scratch.argument_count * sizeof(*site->arguments);
        site = (MinicLP64PreparedCall *)malloc(bytes);
        if (site == NULL) goto failed;
        site->instruction_id = id;
        site->argument_count = scratch.argument_count;
        site->stack_bytes = scratch.stack_bytes;
        if (scratch.argument_count != 0U)
            memcpy(site->arguments, scratch.arguments,
                   scratch.argument_count * sizeof(*site->arguments));
        cache->sites[ordinal] = site;
        previous_id = id;
        if (semantic_calls == NULL) ++id;
    }
    return true;
failed:
    minic_lp64_call_plan_cache_destroy(cache);
    return false;
}

const MinicLP64PreparedCall *minic_lp64_function_call_plan(
    const MinicLP64CallPlanCache *cache, size_t instruction_id) {
    size_t low = 0U, high;
    if (cache == NULL || (cache->call_count != 0U && cache->sites == NULL))
        return NULL;
    high = cache->call_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2U;
        if (cache->sites[mid] == NULL) return NULL;
        if (cache->sites[mid]->instruction_id < instruction_id)
            low = mid + 1U;
        else
            high = mid;
    }
    return low < cache->call_count && cache->sites[low] != NULL &&
           cache->sites[low]->instruction_id == instruction_id
               ? cache->sites[low] : NULL;
}

void minic_lp64_call_plan_cache_destroy(MinicLP64CallPlanCache *cache) {
    size_t i;
    if (cache == NULL) return;
    for (i = 0U; i < cache->call_count; ++i)
        free(cache->sites[i]);
    free(cache->sites);
    cache->sites = NULL;
    cache->call_count = 0U;
}
