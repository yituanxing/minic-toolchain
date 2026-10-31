#include "target/riscv64/core_admission.h"
#include "target/common/core_instruction_family.h"
#include "target/riscv64/core_codegen.h"
#include "target/riscv64/legalize.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/inline_asm_emit.h"
#include "target/riscv64/frame_requirements.h"
#include "target/riscv64/function_abi_plan.h"
#include "target/riscv64/codegen_internal.h"
#include "target/data_layout.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Canonical Core verification belongs to Common FunctionPlan. RV64 owns
 * target capability, psABI placements and strict machine ISA admission. */
static bool core_instruction_supported(const MinicC0Program *program,
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
        return minic_riscv64_core_call_frame_address_supported(instruction);
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
        return program != NULL && abi_plan != NULL && abi_plan->prepared &&
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
        return minic_riscv64_core_record_load_supported(program, function, instruction, NULL);
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        return minic_riscv64_core_record_copy_supported(program, function, instruction);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegisterBinding *binding;

        binding = minic_riscv64_inline_asm_fixed_register_binding(
            function, instruction->value.fixed_register_binding_id);
        return binding != NULL && binding->register_name != NULL &&
               binding->register_name_length != 0U && minic_riscv64_scalar_type(binding->type) &&
               minic_type_equal(binding->type, instruction->type);
    }
    /* Core already proved block bounds, global/function symbol identity,
     * and exact pointer result types. No RV64 ISA feature is involved. */
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        return true;
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicType result_type;
        size_t result_size;
        bool is_unsigned;

        return minic_riscv64_core_integer_overflow_supported(
            program, function, instruction, &result_type, &result_size, &is_unsigned);
    }
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return minic_riscv64_inline_asm_instruction_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL:
        return minic_riscv64_core_direct_call_supported(
            program, function, instruction, semantic_view, prepared_call);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return minic_riscv64_core_indirect_call_supported(
            program, function, instruction, semantic_view, prepared_call);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return minic_riscv64_core_field_address_supported(instruction, NULL);
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return minic_riscv64_core_scalar_bitcast_supported(program, function, instruction);
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
                                   const MinicC0Program *program,
                                   const MinicCoreFunction *function,
                                   const MinicRiscv64FunctionAbiPlan *abi_plan,
                                   const MinicCoreCallIndex *semantic_calls,
                                   const MinicCoreObjectStoragePlan *storage) {
    size_t index, call_site = 0U;
    /* Only the verified immutable function and Common's sorted CALL views
     * reach this routine. ABI and ISA capability remain RV64-owned. */
    if (program == NULL) {
        if (!minic_type_is_void(function->return_type) &&
            !minic_riscv64_scalar_type(function->return_type)) {
            return core_rv64_capability_reject(function, "legacy-return", 0U, -1);
        }
        for (index = 0U; index < function->parameter_count; ++index) {
            if (!minic_riscv64_scalar_type(function->parameter_types[index])) {
                return core_rv64_capability_reject(function, "legacy-parameter", index, -1);
            }
        }
    } else if (abi_plan == NULL || !abi_plan->prepared ||
               abi_plan->parameter_location_count != function->parameter_count ||
               abi_plan->outgoing_calls.count != semantic_calls->count ||
               (semantic_calls->count != 0U &&
                abi_plan->outgoing_calls.sites == NULL) ||
               (function->parameter_count != 0U &&
                abi_plan->parameter_locations == NULL)) {
        /* The function ABI planner owns *all* return/parameter capability
         * checks. Do not second-guess its immutable, qualified placements. */
        return core_rv64_capability_reject(function, "function-abi", 0U, -1);
    }
    /* Common owns all object type size, element_count and explicit alignment
     * calculations. In production borrow the exact Common FunctionPlan
     * snapshot: no second DataLayout walk in RV64 capability admission.
     * Standalone callers retain a checked Common helper, with the original
     * null-program scalar-only compatibility path. */
    if (storage != NULL &&
        (storage->count != function->object_count ||
         (storage->count != 0U && storage->objects == NULL)))
        return core_rv64_capability_reject(function, "object-plan", 0U, -1);
    for (index = 0U; index < function->object_count; ++index) {
        const MinicType type = function->objects[index].type;
        MinicCoreObjectStorage calculated = {0};
        const MinicCoreObjectStorage *shape = NULL;
        if (!minic_riscv64_scalar_type(type) &&
            !minic_type_is_record(type) && !minic_type_is_array(type))
            return core_rv64_capability_reject(function, "object-type", index, -1);
        if (storage != NULL) {
            shape = &storage->objects[index];
        } else if (program != NULL) {
            if (!minic_core_object_storage(
                    program, minic_default_data_layout(),
                    &function->objects[index], &calculated))
                return core_rv64_capability_reject(function, "object-layout", index, -1);
            shape = &calculated;
        } else {
            /* Legacy no-program API cannot resolve record layouts. */
            size_t size, alignment;
            if (!minic_data_layout_type(
                    minic_default_data_layout(), NULL, type, &size, &alignment) ||
                function->objects[index].element_count == 0U ||
                size > SIZE_MAX / function->objects[index].element_count ||
                (function->objects[index].explicit_alignment != 0U &&
                 (function->objects[index].explicit_alignment &
                  (function->objects[index].explicit_alignment - 1U)) != 0U))
                return core_rv64_capability_reject(function, "legacy-object", index, -1);
            calculated.extent = size * function->objects[index].element_count;
            calculated.alignment = alignment;
            shape = &calculated;
        }
        if ((shape->extent == 0U && !minic_type_is_record(type)) ||
            shape->alignment == 0U ||
            (shape->alignment & (shape->alignment - 1U)) != 0U)
            return core_rv64_capability_reject(function, "object", index, -1);
    }
    /* Core verifier already checks global name, type and symbol shape.
     * Relocations and actual address emission remain machine-owned. */
    for (index = 0U; index < function->value_count; ++index) {
        if (!minic_riscv64_scalar_type(function->values[index].type)) {
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
            if (program != NULL) {
                prepared = abi_plan->outgoing_calls.sites[call_site];
                if (prepared == NULL || prepared->instruction_id != index ||
                    prepared->argument_count != view->argument_count)
                    return core_rv64_capability_reject(
                        function, "prepared-call", index, (int)ins->kind);
            }
            ++call_site;
        }
        if (!core_instruction_supported(
                program, function, ins, view, prepared, abi_plan)) {
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
    const MinicC0Program *program, const MinicCoreFunction *function,
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
    if (program != NULL &&
        (abi_plan == NULL || !abi_plan->prepared ||
         !minic_riscv64_call_plan_cache_build(
             program, function,
             abi_plan->named_parameter_cursor.lp64_soft_float,
             calls.index, &abi_plan->outgoing_calls))) {
        minic_core_call_index_handle_destroy(&calls);
        return core_rv64_capability_reject(function, "call-abi", 0U, -1);
    }
    ok = core_function_can_emit_with_index(
        program, function, abi_plan, calls.index, storage);
    minic_core_call_index_handle_destroy(&calls);
    return ok;
}

bool minic_riscv64_core_function_admit_profile(
    const MinicC0Program *program, const MinicCoreFunction *function,
    MinicRiscv64FunctionAbiPlan *abi_plan, bool lp64_soft_float,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage) {
    const bool core_verified =
        minic_core_function_facts_verified(facts, function) ||
        minic_core_function_verify(function);
    if (!core_verified)
        return core_rv64_capability_reject(function, "verify", 0U, -1);
    if (program != NULL &&
        (abi_plan == NULL ||
         !minic_riscv64_function_abi_plan_build_profile(
             program, function, abi_plan, lp64_soft_float)))
        return core_rv64_capability_reject(function, "function-abi", 0U, -1);
    return core_function_can_emit(
        program, function, abi_plan, core_verified,
        minic_core_function_facts_verified(facts, function) ? facts->call_index : NULL,
        storage);
}
bool minic_riscv64_core_function_admit(
    const MinicC0Program *program, const MinicCoreFunction *function,
    MinicRiscv64FunctionAbiPlan *abi_plan) {
    return minic_riscv64_core_function_admit_profile(
        program, function, abi_plan, false, NULL, NULL);
}
bool minic_riscv64_core_function_can_emit(const MinicCoreFunction *function) {
    return core_function_can_emit(NULL, function, NULL, false, NULL, NULL);
}
bool minic_riscv64_core_function_can_emit_for_program(
    const MinicC0Program *program, const MinicCoreFunction *function) {
    MinicRiscv64FunctionAbiPlan plan = {0};
    bool ok;
    if (program == NULL) return false;
    ok = minic_riscv64_core_function_admit(program, function, &plan);
    minic_riscv64_function_abi_plan_destroy(&plan);
    return ok;
}

