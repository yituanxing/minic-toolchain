#include "target/riscv64/core_admission.h"
#include "target/common/core_instruction_family.h"
#include "target/riscv64/legalize.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/frame_requirements.h"
#include "target/riscv64/function_abi_plan.h"
#include "target/riscv64/core_type_query.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Canonical Core verification belongs to Common FunctionPlan. RV64 owns
 * target capability, psABI placements and strict machine ISA admission. */
static bool core_instruction_supported(const MinicCoreFunction *function,
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
        return abi_plan != NULL && abi_plan->prepared &&
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

        binding = instruction->value.fixed_register_binding_id <
                      function->fixed_register_binding_count &&
                  function->fixed_register_bindings != NULL ?
            &function->fixed_register_bindings[
                instruction->value.fixed_register_binding_id] : NULL;
        if (binding == NULL || binding->register_name == NULL ||
            binding->register_name_length == 0U) return false;
        return minic_riscv64_core_scalar_kind(minic_core_function_base_type(
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
        /* Core verifier owns template and CFG shape; mandatory MachineIR
         * preflight verifies physical operands and continuation effects. */
        return true;
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL:
        return minic_riscv64_core_direct_call_supported(
            function, semantic_view, prepared_call);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return minic_riscv64_core_indirect_call_supported(
            function, semantic_view, prepared_call);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return minic_riscv64_core_field_address_supported(instruction, NULL);
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return minic_riscv64_core_scalar_bitcast_supported(function, instruction);
    default:
        return false;
    }
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
                                   const MinicCoreFunction *function,
                                   const MinicRiscv64FunctionAbiPlan *abi_plan,
                                   const MinicCoreCallIndex *semantic_calls,
                                   const MinicCoreObjectStoragePlan *storage) {
    size_t index, call_site = 0U;
    /* Only the verified immutable function and Common's sorted CALL views
     * reach this routine. ABI and ISA capability remain RV64-owned. */
    if (function == NULL || function->type_context == NULL)
        return core_rv64_capability_reject(function, "sealed-types", 0U, -1);
    if (semantic_calls == NULL || abi_plan == NULL || !abi_plan->prepared ||
        abi_plan->parameter_location_count != minic_core_function_parameter_count(function) ||
        abi_plan->outgoing_calls.count != semantic_calls->count ||
        (semantic_calls->count != 0U && abi_plan->outgoing_calls.sites == NULL) ||
        (minic_core_function_parameter_count(function) != 0U &&
         abi_plan->parameter_locations == NULL))
        return core_rv64_capability_reject(function, "function-abi", 0U, -1);
    /* The Common prepared owner has resolved all object extents and
     * alignments. Never recompute Core layout in a second RV64 path. */
    if (storage == NULL || storage->count != function->object_count ||
        (storage->count != 0U && storage->objects == NULL))
        return core_rv64_capability_reject(function, "object-plan", 0U, -1);
    for (index = 0U; index < function->object_count; ++index) {
        const MinicCoreType *core_type = minic_core_function_base_type(
            function, function->objects[index].core_type_id);
        const bool is_record = minic_riscv64_core_is_record(
            function, function->objects[index].core_type_id);
        const MinicCoreObjectStorage *shape = &storage->objects[index];
        if (!(minic_riscv64_core_scalar_kind(core_type) || is_record ||
              (core_type != NULL && core_type->kind == MINIC_CORE_TYPE_ARRAY)))
            return core_rv64_capability_reject(function, "object-type", index, -1);
        if ((shape->extent == 0U && !is_record) ||
            shape->alignment == 0U ||
            (shape->alignment & (shape->alignment - 1U)) != 0U)
            return core_rv64_capability_reject(function, "object", index, -1);
    }
    /* Core verifier already checks global name, type and symbol shape.
     * Relocations and actual address emission remain machine-owned. */
    for (index = 0U; index < function->value_count; ++index) {
        if (!minic_riscv64_core_scalar_kind(minic_core_function_base_type(
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
            prepared = abi_plan->outgoing_calls.sites[call_site];
            if (prepared == NULL || prepared->instruction_id != index ||
                prepared->argument_count != view->argument_count)
                return core_rv64_capability_reject(
                    function, "prepared-call", index, (int)ins->kind);
            ++call_site;
        }
        if (!core_instruction_supported(
                function, ins, view, prepared, abi_plan)) {
            return core_rv64_capability_reject(
                function, "instruction", index, (int)ins->kind);
        }
    }
    if (call_site != semantic_calls->count)
        return core_rv64_capability_reject(function, "call-index-count",
                                          call_site, -1);
    return true;
}

/* All RV64 production emitters borrow Common's verified Core function and
 * its ordered CALL index. Machine capability, psABI and ISA still fail closed. */
bool minic_riscv64_core_function_admit_profile(
    const MinicCoreFunction *function,
    MinicRiscv64FunctionAbiPlan *abi_plan, bool lp64_soft_float,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage) {
    if (!minic_core_function_facts_verified(facts, function))
        return core_rv64_capability_reject(function, "verify", 0U, -1);
    if (facts->call_index == NULL)
        return core_rv64_capability_reject(function, "call-index", 0U, -1);
    if (abi_plan == NULL ||
        !minic_riscv64_function_abi_plan_build_profile(
            function, abi_plan, lp64_soft_float))
        return core_rv64_capability_reject(function, "function-abi", 0U, -1);
    if (!minic_riscv64_call_plan_cache_build(
            function, abi_plan->named_parameter_cursor.lp64_soft_float,
            facts->call_index, &abi_plan->outgoing_calls))
        return core_rv64_capability_reject(function, "call-abi", 0U, -1);
    return core_function_can_emit_with_index(
        function, abi_plan, facts->call_index, storage);
}
