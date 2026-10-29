#include "target/riscv64/asm_plan.h"
#include "target/riscv/register_identity.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const char *const minic_riscv64_asm_callee_saved_registers[MINIC_RISCV64_ASM_CALLEE_SAVED_COUNT] = {
    "s0", "s1", "s2", "s3", "s4", "s5",
    "s6", "s7", "s8", "s9", "s10", "s11",
};

static const MinicCoreFixedRegisterBinding *core_fixed_register_binding(
    const MinicCoreFunction *function, size_t binding_id) {
    return function != NULL && binding_id < function->fixed_register_binding_count
               ? &function->fixed_register_bindings[binding_id]
               : NULL;
}

static bool core_asm_callee_saved_index_for_mask(const char *name, size_t *result) {
    size_t index;
    if (name == NULL || result == NULL) {
        return false;
    }
    for (index = 0U; index < MINIC_RISCV64_ASM_CALLEE_SAVED_COUNT; ++index) {
        if (minic_riscv_gpr_name_equal(
                name, minic_riscv64_asm_callee_saved_registers[index])) {
            *result = index;
            return true;
        }
    }
    return false;
}

size_t minic_riscv64_asm_callee_saved_mask_count(uint16_t mask) {
    size_t count = 0U;
    size_t index;
    for (index = 0U; index < MINIC_RISCV64_ASM_CALLEE_SAVED_COUNT; ++index) {
        if ((mask & (uint16_t)(1U << index)) != 0U) {
            ++count;
        }
    }
    return count;
}

static bool core_inline_asm_clobbers_register(const MinicCoreInlineAsm *inline_asm,
                                               const char *register_name) {
    size_t index;
    unsigned candidate_id;

    if (inline_asm == NULL ||
        !minic_riscv_gpr_name_id(register_name, &candidate_id)) {
        return true;
    }
    for (index = 0U; index < inline_asm->register_clobber_count; ++index) {
        const MinicCoreInlineAsmRegisterClobber *clobber =
            &inline_asm->register_clobbers[index];
        unsigned clobber_id;
        if (minic_riscv_gpr_id(clobber->name, clobber->name_length,
                               &clobber_id) && clobber_id == candidate_id) {
            return true;
        }
    }
    return false;
}


typedef struct MinicCoreRiscv64AsmRegisterCandidate {
    const char *name;
} MinicCoreRiscv64AsmRegisterCandidate;

static const MinicCoreRiscv64AsmRegisterCandidate core_asm_caller_saved_registers[] = {
    {"t0"}, {"t1"}, {"t2"}, {"t3"}, {"t4"}, {"t5"}, {"t6"},
    {"a0"}, {"a1"}, {"a2"}, {"a3"}, {"a4"}, {"a5"}, {"a6"}, {"a7"},
};

static bool core_asm_register_name_equal(const char *left, const char *right) {
    return minic_riscv_gpr_name_equal(left, right);
}

static bool core_asm_register_is_caller_saved(const char *name) {
    unsigned id;
    return minic_riscv_gpr_name_id(name, &id) &&
           minic_riscv_gpr_caller_saved(id);
}


static bool core_asm_register_is_callee_saved(const char *name) {
    unsigned id;
    return minic_riscv_gpr_name_id(name, &id) &&
           minic_riscv_gpr_callee_saved(id);
}

static bool core_asm_register_in_use(const char *const *operand_registers,
                                     size_t operand_count,
                                     const char *name) {
    size_t index;
    if (operand_registers == NULL || name == NULL) {
        return true;
    }
    for (index = 0U; index < operand_count; ++index) {
        if (core_asm_register_name_equal(operand_registers[index], name)) {
            return true;
        }
    }
    return false;
}

static const char *core_asm_choose_register(const MinicCoreInlineAsm *inline_asm,
                                            const char *const *operand_registers,
                                            size_t operand_count,
                                            const char *const *preferences,
                                            size_t preference_count) {
    size_t index;
    for (index = 0U; index < preference_count; ++index) {
        const char *candidate = preferences[index];
        if (!core_inline_asm_clobbers_register(inline_asm, candidate) &&
            !core_asm_register_in_use(operand_registers, operand_count, candidate)) {
            return candidate;
        }
    }
    return NULL;
}

/* M166_RV64_FIXED_ASM_PHASE_ALIAS: two distinct fixed-register operands may
   intentionally share one architectural register when one is a write-only
   output and the other is a scalar input. Their lifetimes are disjoint:
   input before asm, output after asm. Early-clobber outputs cannot alias. */
static bool core_structured_fixed_phase_alias_safe(
    const MinicCoreInstruction *instruction,
    size_t current_binding_index,
    const char *const *operand_registers,
    const char *register_name) {
    const MinicCoreStructuredInlineAsmOperand *current;
    size_t alias_count;
    size_t prior_index;

    if (instruction == NULL || operand_registers == NULL || register_name == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        current_binding_index >= instruction->value.structured_inline_asm.operand_count) {
        return false;
    }
    current = &instruction->value.structured_inline_asm.operands[current_binding_index];
    if (!current->has_fixed_register_binding) {
        return false;
    }
    alias_count = 0U;
    for (prior_index = 0U; prior_index < current_binding_index; ++prior_index) {
        const MinicCoreStructuredInlineAsmOperand *prior =
            &instruction->value.structured_inline_asm.operands[prior_index];
        bool current_is_input;
        bool current_is_output;
        bool prior_is_input;
        bool prior_is_output;
        const MinicCoreStructuredInlineAsmOperand *output;

        if (!prior->has_fixed_register_binding || prior->operand_index > 9U ||
            !core_asm_register_name_equal(
                operand_registers[prior->operand_index], register_name)) {
            continue;
        }
        alias_count += 1U;
        if (alias_count != 1U) {
            return false;
        }
        current_is_input =
            current->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        current_is_output =
            current->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        prior_is_input = prior->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        prior_is_output = prior->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        if (!((current_is_input && prior_is_output) ||
              (current_is_output && prior_is_input))) {
            return false;
        }
        output = current_is_output ? current : prior;
        if (output->early_clobber) {
            return false;
        }
    }
    return alias_count == 1U;
}

static bool core_structured_inline_asm_allocate(
    const MinicCoreFunction *function,
    const MinicCoreInstruction *instruction,
    const char **operand_registers,
    bool *memory_operand,
    const char **scratch_register) {
    static const char *const output_preferences[] = {
        "t0", "t1", "t2", "t3", "t4", "t5", "t6",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s0",
    };
    static const char *const memory_preferences[] = {
        "t2", "t6", "t5", "t4", "t3", "t1", "t0",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s0",
    };
    static const char *const input_preferences[] = {
        "t3", "t4", "t5", "t6", "t2", "t1", "t0",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s0",
    };
    const MinicCoreInlineAsm *inline_asm;
    size_t operand_count;
    size_t binding_index;
    size_t clobber_index;

    if (function == NULL || instruction == NULL ||
        operand_registers == NULL || memory_operand == NULL || scratch_register == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    operand_count = instruction->value.structured_inline_asm.operand_count;
    /* Core admits up to sixteen source operands. This machine plan has only
     * ten physical bindings: reject before indexing either ten-slot array. */
    if (operand_count == 0U || operand_count > MINIC_RISCV64_ASM_MAX_OPERANDS) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
    for (binding_index = 0U; binding_index < 10U; ++binding_index) {
        operand_registers[binding_index] = NULL;
        memory_operand[binding_index] = false;
    }

    /* M172: explicit callee-saved clobbers are valid because the function
       frame now preserves the complete callee-saved bank. Unknown architectural
       register names still fail closed. */
    for (clobber_index = 0U; clobber_index < inline_asm->register_clobber_count;
         ++clobber_index) {
        const MinicCoreInlineAsmRegisterClobber *clobber =
            &inline_asm->register_clobbers[clobber_index];
        if (clobber->name == NULL ||
            (!core_asm_register_is_caller_saved(clobber->name) &&
             !core_asm_register_is_callee_saved(clobber->name))) {
            return false;
        }
    }

    /* Reserve all fixed bindings before generic allocation so source order
       cannot accidentally steal a required architectural register. */
    for (binding_index = 0U; binding_index < operand_count; ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const MinicCoreFixedRegisterBinding *fixed_binding;
        if (binding->operand_index >= MINIC_RISCV64_ASM_MAX_OPERANDS)
            return false;
        if (!binding->has_fixed_register_binding) {
            continue;
        }
        if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
            binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
            binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {
            return false;
        }
        fixed_binding =
            core_fixed_register_binding(function, binding->fixed_register_binding_id);
        if (fixed_binding == NULL || !fixed_binding->is_local ||
            fixed_binding->register_name == NULL || fixed_binding->register_name_length == 0U ||
            core_inline_asm_clobbers_register(inline_asm, fixed_binding->register_name)) {
            return false;
        }
        if (core_asm_register_in_use(operand_registers, 10U, fixed_binding->register_name) &&
            !core_structured_fixed_phase_alias_safe(
                instruction, binding_index, operand_registers, fixed_binding->register_name)) {
            return false;
        }
        operand_registers[binding->operand_index] = fixed_binding->register_name;
    }

    for (binding_index = 0U; binding_index < operand_count; ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const char *register_name;
        const char *const *preferences;
        size_t preference_count;

        if (binding->operand_index >= MINIC_RISCV64_ASM_MAX_OPERANDS)
            return false;
        if (operand_registers[binding->operand_index] != NULL) {
            continue;
        }
        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            preferences = output_preferences;
            preference_count = sizeof(output_preferences) / sizeof(output_preferences[0]);
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            preferences = memory_preferences;
            preference_count = sizeof(memory_preferences) / sizeof(memory_preferences[0]);
            memory_operand[binding->operand_index] = true;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            preferences = input_preferences;
            preference_count = sizeof(input_preferences) / sizeof(input_preferences[0]);
            break;
        default:
            return false;
        }
        register_name = core_asm_choose_register(
            inline_asm, operand_registers, 10U, preferences, preference_count);
        if (register_name == NULL) {
            return false;
        }
        operand_registers[binding->operand_index] = register_name;
    }

    *scratch_register = NULL;
    for (binding_index = 0U;
         binding_index < sizeof(core_asm_caller_saved_registers) /
                             sizeof(core_asm_caller_saved_registers[0]);
         ++binding_index) {
        const char *candidate = core_asm_caller_saved_registers[binding_index].name;
        /* Scratch is used only before/after the asm, so an asm clobber is fine;
           it merely must not alias a live operand register. */
        if (!core_asm_register_in_use(operand_registers, 10U, candidate)) {
            *scratch_register = candidate;
            break;
        }
    }
    return *scratch_register != NULL;
}


bool minic_riscv64_asm_instruction_plan_build(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction,
    MinicRiscv64AsmInstructionPlan *plan) {
    if (plan == NULL) return false;
    memset(plan, 0, sizeof(*plan));
    if (!core_structured_inline_asm_allocate(
            function, instruction, plan->operand_registers, plan->memory_operand,
            &plan->scratch_register)) return false;
    plan->valid = true;
    return true;
}

void minic_riscv64_asm_function_plan_destroy(MinicRiscv64AsmFunctionPlan *plan) {
    if (plan == NULL) return;
    free(plan->instructions);
    memset(plan, 0, sizeof(*plan));
}

const MinicRiscv64AsmInstructionPlan *minic_riscv64_asm_function_plan_get(
    const MinicRiscv64AsmFunctionPlan *plan, size_t instruction_id) {
    if (plan == NULL || plan->instructions == NULL ||
        instruction_id >= plan->instruction_count ||
        !plan->instructions[instruction_id].valid) return NULL;
    return &plan->instructions[instruction_id];
}

bool minic_riscv64_asm_function_plan_build(
    const MinicCoreFunction *function, MinicRiscv64AsmFunctionPlan *plan) {
    size_t i;
    if (function == NULL || plan == NULL) return false;
    memset(plan, 0, sizeof(*plan));
    if (function->instruction_count == 0U) return true;
    if (function->instructions == NULL) return false;
    /* Most functions have no structured asm: avoid allocating a table for
     * every Core instruction on the ordinary code generation path. */
    for (i = 0U; i < function->instruction_count; ++i) {
        if (function->instructions[i].kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM)
            break;
    }
    if (i == function->instruction_count) return true;
    if (function->instruction_count > SIZE_MAX / sizeof(*plan->instructions)) return false;
    plan->instructions = (MinicRiscv64AsmInstructionPlan *)calloc(
        function->instruction_count, sizeof(*plan->instructions));
    if (plan->instructions == NULL) return false;
    plan->instruction_count = function->instruction_count;
    for (i = 0U; i < function->instruction_count; ++i) {
        const MinicCoreInstruction *inst = &function->instructions[i];
        const MinicCoreInlineAsm *asm_data;
        size_t j;
        if (inst->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) continue;
        if (inst->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
            !minic_riscv64_asm_instruction_plan_build(
                function, inst, &plan->instructions[i])) {
            minic_riscv64_asm_function_plan_destroy(plan);
            return false;
        }
        for (j = 0U; j < MINIC_RISCV64_ASM_MAX_OPERANDS; ++j) {
            size_t saved_index;
            const char *name = plan->instructions[i].operand_registers[j];
            if (name != NULL && core_asm_callee_saved_index_for_mask(name, &saved_index))
                plan->callee_saved_mask |= (uint16_t)(1U << saved_index);
        }
        asm_data = &function->inline_asms[inst->value.structured_inline_asm.inline_asm_id];
        for (j = 0U; j < asm_data->register_clobber_count; ++j) {
            size_t saved_index;
            const char *name = asm_data->register_clobbers[j].name;
            if (name != NULL && core_asm_callee_saved_index_for_mask(name, &saved_index))
                plan->callee_saved_mask |= (uint16_t)(1U << saved_index);
        }
    }
    return true;
}
