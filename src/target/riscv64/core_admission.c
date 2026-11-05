#include "target/riscv64/core_admission.h"
#include "target/common/core_instruction_family.h"
#include "target/riscv64/core_codegen.h"
#include "target/riscv64/legalize.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/inline_asm_emit.h"
#include "target/riscv64/frame_requirements.h"
#include "target/riscv64/function_abi_plan.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/core_type_query.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const MinicCoreType *rv64_admit_core_base(
    const MinicCoreFunction *function, MinicCoreTypeId id) {
    if (function == NULL || function->type_context == NULL) return NULL;
    return minic_core_type_get(function->type_context,
        minic_core_type_unqualified(function->type_context, id));
}

static bool rv64_admit_core_scalar(const MinicCoreType *type) {
    return type != NULL &&
        (type->kind == MINIC_CORE_TYPE_INTEGER ||
         type->kind == MINIC_CORE_TYPE_POINTER ||
         (type->kind == MINIC_CORE_TYPE_FLOAT &&
          (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 ||
           type->value.floating.format == MINIC_CORE_FLOAT_BINARY64 ||
           type->value.floating.format == MINIC_CORE_FLOAT_BINARY128)));
}

/* Canonical Core verification belongs to Common FunctionPlan. RV64 owns
 * target capability, psABI placements and strict machine ISA admission. */
static bool core_instruction_supported(bool require_prepared_abi,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction,
                                       const MinicCoreCallView *semantic_view,
                                       const MinicRiscv64PreparedCall *prepared_call,
                                       const MinicRiscv64FunctionAbiPlan *abi_plan) {
    if (function == NULL || instruction == NULL) {
        return false;
    }
    /* A single Core semantic vocabulary for x86, RV32/RV64 and Direct
     * LP64. This only admits Core shapes into further target checks, NOT
     * a hardware opcode/extension (strict I remains target-owned). */
    const MinicCoreSemanticFamily family =
        minic_core_instruction_semantic_family(instruction->kind);
    if (family == MINIC_CORE_FAMILY_INTEGER_SCALAR ||
        family == MINIC_CORE_FAMILY_FLOATING_SCALAR)
        return true;
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        return minic_riscv64_core_call_frame_address_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
        /* Pointer result and unsigned-long operand were checked by the
         * canonical Core verifier before entering target admission. */
        return true;
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
        /* RV64 atomic ISA/memory-order lowering is not qualified yet.
         * Never map Core atomic.store to an ordinary unqualified store. */
        return false;
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY:
        /* SysV 24-byte cursor copying is x86-only; RV64 keeps its linear
         * cursor-assignment path and must reject this opcode explicitly. */
        return false;
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR:
        /* This typed SysV variadic read must never silently reuse the
         * existing RV64 8-byte linear va_arg lowering. */
        return false;
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {
        size_t fixed_stack_slots;
        size_t integer_parameter_count;

        /* Parameter locations have already been classified by the same
         * qualified psABI plan used by incoming machine emission. Do not
         * re-run the named prefix cursor for every va_start instruction. */
        return require_prepared_abi && abi_plan != NULL && abi_plan->prepared &&
               minic_riscv64_abi_variadic_prefix_from_cursor(
                   &abi_plan->named_parameter_cursor,
                   &integer_parameter_count, &fixed_stack_slots);
    }
    case MINIC_CORE_INSTRUCTION_PARAMETER:
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
        return true;
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
        return minic_riscv64_core_record_load_supported(function, instruction, NULL);
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        return minic_riscv64_core_record_copy_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegisterBinding *binding;

        binding = minic_riscv64_inline_asm_fixed_register_binding(
            function, instruction->value.fixed_register_binding_id);
        if (binding == NULL || binding->register_name == NULL ||
            binding->register_name_length == 0U) return false;
        return rv64_admit_core_scalar(rv64_admit_core_base(
                   function, binding->core_type_id)) &&
               binding->core_type_id == instruction->core_type_id;
    }
    /* Core already proved block bounds, global/function symbol identity,
     * and exact pointer result types. No RV64 ISA feature is involved. */
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        return true;
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicCoreTypeId result_type;
        size_t result_size;
        bool is_unsigned;

        return minic_riscv64_core_integer_overflow_supported(
            function, instruction, &result_type, &result_size, &is_unsigned);
    }
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return minic_riscv64_inline_asm_instruction_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL:
        return minic_riscv64_core_direct_call_supported(
            require_prepared_abi, function, instruction, semantic_view, prepared_call);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return minic_riscv64_core_indirect_call_supported(
            require_prepared_abi, function, instruction, semantic_view, prepared_call);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return minic_riscv64_core_field_address_supported(instruction, NULL);
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return minic_riscv64_core_scalar_bitcast_supported(function, instruction);
    default:
        return false;
    }
    return false;
}

static bool core_rv64_capability_reject(const MinicCoreFunction *function,
                                        const char *stage,
                                        size_t index,
                                        int instruction_kind) {
    const char *trace;

    trace = getenv("CORE_FAST_TRACE");
    if (trace != NULL && trace[0] != '\0' && strcmp(trace, "0") != 0) {
        fprintf(stderr,
                "CORE_RV64_CAP function=%.*s stage=%s index=%zu instruction_kind=%d\n",
                function == NULL ? 0 : (int)function->name_length,
                function == NULL || function->name == NULL ? "" : function->name,
                stage == NULL ? "unknown" : stage,
                index,
                instruction_kind);
    }
    return false;
}

static bool core_function_can_emit_with_index(
                                   bool require_prepared_abi,
                                   const MinicCoreFunction *function,
                                   const MinicRiscv64FunctionAbiPlan *abi_plan,
                                   const MinicCoreCallIndex *semantic_calls,
                                   const MinicCoreObjectStoragePlan *storage) {
    size_t index, call_site = 0U;
    /* Only the verified immutable function and Common's sorted CALL views
     * reach this routine. ABI and ISA capability remain RV64-owned. */
    if (function == NULL || function->type_context == NULL)
        return core_rv64_capability_reject(function, "sealed-types", 0U, -1);
    if (!require_prepared_abi) {
        const MinicCoreType *ret = rv64_admit_core_base(
            function, minic_core_function_return_type(function));
        if (ret == NULL ||
            (minic_core_function_parameter_count(function) != 0U &&
             minic_core_function_parameter_types(function) == NULL) ||
            (ret->kind != MINIC_CORE_TYPE_VOID && !rv64_admit_core_scalar(ret)))
            return core_rv64_capability_reject(function, "core-return", 0U, -1);
        for (index = 0U; index < minic_core_function_parameter_count(function); ++index) {
            if (!rv64_admit_core_scalar(rv64_admit_core_base(
                    function, minic_core_function_parameter_types(function)[index])))
                return core_rv64_capability_reject(function, "core-parameter", index, -1);
        }
    } else if (abi_plan == NULL || !abi_plan->prepared ||
               abi_plan->parameter_location_count != minic_core_function_parameter_count(function) ||
               abi_plan->outgoing_calls.count != semantic_calls->count ||
               (semantic_calls->count != 0U &&
                abi_plan->outgoing_calls.sites == NULL) ||
               (minic_core_function_parameter_count(function) != 0U &&
                abi_plan->parameter_locations == NULL)) {
        /* The function ABI planner owns *all* return/parameter capability
         * checks. Do not second-guess its immutable, qualified placements. */
        return core_rv64_capability_reject(function, "function-abi", 0U, -1);
    }
    /* Common owns all object size/count/alignment calculations. Production
     * borrows the exact FunctionPlan snapshot, standalone callers calculate
     * from the same sealed CoreType layout without reopening C DataLayout. */
    if (storage != NULL &&
        (storage->count != function->object_count ||
         (storage->count != 0U && storage->objects == NULL)))
        return core_rv64_capability_reject(function, "object-plan", 0U, -1);
    for (index = 0U; index < function->object_count; ++index) {
        const MinicCoreType *core_type = rv64_admit_core_base(
            function, function->objects[index].core_type_id);
        const bool is_record = minic_riscv64_core_is_record(
            function, function->objects[index].core_type_id);
        MinicCoreObjectStorage calculated = {0};
        const MinicCoreObjectStorage *shape = NULL;
        if (!(rv64_admit_core_scalar(core_type) || is_record ||
              (core_type != NULL && core_type->kind == MINIC_CORE_TYPE_ARRAY)))
            return core_rv64_capability_reject(function, "object-type", index, -1);
        if (storage != NULL) {
            shape = &storage->objects[index];
        } else {
            /* Even standalone RV64 admission must not reopen C layout once
             * the Core function has a resolved execution-type snapshot. */
            if (!minic_core_object_storage_resolved(
                    function->type_context, &function->objects[index], &calculated))
                return core_rv64_capability_reject(function, "object-layout", index, -1);
            shape = &calculated;
        }
        if ((shape->extent == 0U && !is_record) ||
            shape->alignment == 0U ||
            (shape->alignment & (shape->alignment - 1U)) != 0U)
            return core_rv64_capability_reject(function, "object", index, -1);
    }
    /* Core verifier already checks global name, type and symbol shape.
     * Relocations and actual address emission remain machine-owned. */
    for (index = 0U; index < function->value_count; ++index) {
        if (!rv64_admit_core_scalar(rv64_admit_core_base(
                function, function->values[index].core_type_id))) {
            return core_rv64_capability_reject(function, "value", index, -1);
        }
    }
    for (index = 0U; index < function->instruction_count; ++index) {
        const MinicCoreInstruction *ins = &function->instructions[index];
        const MinicCoreCallView *view = NULL;
        const MinicRiscv64PreparedCall *prepared = NULL;
        if (minic_core_instruction_is_call(ins->kind)) {
            /* The Common index is ordered by instruction ID. Consume its
             * immutable CALL views in lockstep with RV64's required ISA
             * capability walk, without a binary search at each CALL. */
            if (call_site >= semantic_calls->count ||
                semantic_calls->sites[call_site].instruction_id != index)
                return core_rv64_capability_reject(
                    function, "call-index", index, (int)ins->kind);
            view = &semantic_calls->sites[call_site].view;
            if (require_prepared_abi) {
                prepared = abi_plan->outgoing_calls.sites[call_site];
                if (prepared == NULL || prepared->instruction_id != index ||
                    prepared->argument_count != view->argument_count)
                    return core_rv64_capability_reject(
                        function, "prepared-call", index, (int)ins->kind);
            }
            ++call_site;
        }
        if (!core_instruction_supported(
                require_prepared_abi, function, ins, view, prepared, abi_plan)) {
            return core_rv64_capability_reject(
                function, "instruction", index, (int)ins->kind);
        }
    }
    if (call_site != semantic_calls->count)
        return core_rv64_capability_reject(function, "call-index-count",
                                          call_site, -1);
    return true;
}

/* The production visitor lends its prepared index. Standalone Core
 * admission builds the identical Common semantic index, not a second
 * RV64-only CALL scan/shape resolver. */
static bool core_function_can_emit(
    bool require_prepared_abi, const MinicCoreFunction *function,
    MinicRiscv64FunctionAbiPlan *abi_plan, bool core_verified,
    const MinicCoreCallIndex *semantic_calls,
    const MinicCoreObjectStoragePlan *storage) {
    MinicCoreCallIndexHandle calls = {0};
    bool ok;
    if (function == NULL ||
        (!core_verified && !minic_core_function_verify(function)))
        return core_rv64_capability_reject(function, "verify", 0U, -1);
    if (!minic_core_call_index_handle_prepare(function, semantic_calls, &calls))
        return core_rv64_capability_reject(function, "call-index", 0U, -1);
    /* Build the outgoing psABI exactly once with the incoming ABI profile.
     * Admission, frame stack sizing and machine emission then share these
     * same immutable locations, including LP64 versus LP64D FP records. */
    if (require_prepared_abi &&
        (abi_plan == NULL || !abi_plan->prepared ||
         !minic_riscv64_call_plan_cache_build(
             function,
             abi_plan->named_parameter_cursor.lp64_soft_float,
             calls.index, &abi_plan->outgoing_calls))) {
        minic_core_call_index_handle_destroy(&calls);
        return core_rv64_capability_reject(function, "call-abi", 0U, -1);
    }
    ok = core_function_can_emit_with_index(
        require_prepared_abi, function, abi_plan, calls.index, storage);
    minic_core_call_index_handle_destroy(&calls);
    return ok;
}

bool minic_riscv64_core_function_admit_profile(
    bool require_prepared_abi, const MinicCoreFunction *function,
    MinicRiscv64FunctionAbiPlan *abi_plan, bool lp64_soft_float,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage) {
    const bool core_verified =
        minic_core_function_facts_verified(facts, function) ||
        minic_core_function_verify(function);
    if (!core_verified)
        return core_rv64_capability_reject(function, "verify", 0U, -1);
    if (require_prepared_abi &&
        (abi_plan == NULL ||
         !minic_riscv64_function_abi_plan_build_profile(
             function, abi_plan, lp64_soft_float)))
        return core_rv64_capability_reject(function, "function-abi", 0U, -1);
    return core_function_can_emit(
        require_prepared_abi, function, abi_plan, core_verified,
        minic_core_function_facts_verified(facts, function) ? facts->call_index : NULL,
        storage);
}
bool minic_riscv64_core_function_admit(
    bool require_prepared_abi, const MinicCoreFunction *function,
    MinicRiscv64FunctionAbiPlan *abi_plan) {
    return minic_riscv64_core_function_admit_profile(
        require_prepared_abi, function, abi_plan, false, NULL, NULL);
}
bool minic_riscv64_core_function_can_emit(const MinicCoreFunction *function) {
    return core_function_can_emit(false, function, NULL, false, NULL, NULL);
}

