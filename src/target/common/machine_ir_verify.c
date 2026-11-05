#include "target/common/machine_ir_verify.h"
#include "core/symbol_identity.h"

#include <stdlib.h>
#include <string.h>

#define MACHINE_VALID_ACCESS ((unsigned int)(MINIC_MACHINE_REGISTER_READ | \
    MINIC_MACHINE_REGISTER_WRITE | MINIC_MACHINE_REGISTER_CLOBBER))
#define MACHINE_VALID_EFFECTS ((unsigned int)(MINIC_MACHINE_EFFECT_MEMORY_READ | \
    MINIC_MACHINE_EFFECT_MEMORY_WRITE | MINIC_MACHINE_EFFECT_VOLATILE | \
    MINIC_MACHINE_EFFECT_COMPILER_BARRIER | MINIC_MACHINE_EFFECT_CALL | \
    MINIC_MACHINE_EFFECT_BRANCH | MINIC_MACHINE_EFFECT_RETURN | \
    MINIC_MACHINE_EFFECT_SOURCE_ASM))

static bool machine_power_of_two(size_t number) {
    return number != 0U && (number & (number - 1U)) == 0U;
}

static bool machine_validate_register(
    const MinicMachineFunction *function, const MinicMachineVerifyPolicy *policy,
    const MinicMachineRegister *reg) {
    return reg != NULL && reg->bank >= MINIC_MACHINE_REGISTER_GENERAL &&
        reg->bank <= MINIC_MACHINE_REGISTER_SYSTEM &&
        reg->width_bits != 0U && reg->width_bits <= 2048U &&
        policy->validate_register != NULL &&
        policy->validate_register(function->target, reg, policy->context);
}

static bool machine_validate_reg_effect(
    const MinicMachineFunction *function, const MinicMachineVerifyPolicy *policy,
    const MinicMachineRegisterEffect *effect) {
    return effect != NULL && effect->access != 0U &&
        (effect->access & ~MACHINE_VALID_ACCESS) == 0U &&
        machine_validate_register(function, policy, &effect->reg);
}

static bool machine_same_register(const MinicMachineRegister *left,
                                  const MinicMachineRegister *right) {
    return left->number == right->number &&
        left->bank == right->bank &&
        left->width_bits == right->width_bits;
}

static bool machine_validate_frames(const MinicMachineFunction *function) {
    size_t i;
    if (!machine_power_of_two(function->frame_alignment) ||
        (function->frame_size % function->frame_alignment) != 0U ||
        function->frame_object_count > function->frame_object_capacity ||
        (function->frame_object_count != 0U &&
         function->frame_objects == NULL)) return false;
    for (i = 0U; i < function->frame_object_count; ++i) {
        const MinicMachineFrameObject *obj = &function->frame_objects[i];
        uint64_t distance;
        if (obj->size_bytes == 0U ||
            !machine_power_of_two(obj->alignment_bytes) ||
            obj->area > MINIC_MACHINE_FRAME_OUTGOING ||
            !obj->has_fixed_offset) return false;
        distance = obj->fixed_offset < 0
            ? (uint64_t)(-(obj->fixed_offset + INT64_C(1))) + 1U
            : (uint64_t)obj->fixed_offset;
        if (distance % obj->alignment_bytes != 0U) return false;
        if (obj->area == MINIC_MACHINE_FRAME_LOCAL) {
            /* A target may address locals via positive SP offsets or
             * negative FP offsets. Do not silently wrap either form. */
            if (distance > (uint64_t)function->frame_size ||
                obj->size_bytes > function->frame_size) return false;
            if (obj->fixed_offset >= 0 &&
                obj->size_bytes > function->frame_size - (size_t)distance)
                return false;
        }
    }
    return true;
}

static bool machine_operand_valid(const MinicMachineFunction *function,
    const MinicMachineVerifyPolicy *policy, const MinicMachineOperand *operand) {
    const MinicMachineAddress *address;
    if (operand == NULL) return false;
    switch (operand->kind) {
    case MINIC_MACHINE_OPERAND_REGISTER:
        return operand->value.physical.access != 0U &&
            (operand->value.physical.access & ~MACHINE_VALID_ACCESS) == 0U &&
            machine_validate_register(
                function, policy, &operand->value.physical.reg);
    case MINIC_MACHINE_OPERAND_INTEGER:
        return true;
    case MINIC_MACHINE_OPERAND_FRAME:
        if (operand->value.frame.index < function->frame_object_count) {
            const int64_t base = function->frame_objects[
                operand->value.frame.index].fixed_offset;
            const int64_t addend = operand->value.frame.displacement;
            return (addend >= 0 || base >= INT64_MIN - addend) &&
                   (addend <= 0 || base <= INT64_MAX - addend);
        }
        return false;
    case MINIC_MACHINE_OPERAND_SYMBOL:
        return minic_core_assembler_symbol_lexically_safe(
                   operand->value.symbol.name) &&
            policy->validate_relocation != NULL &&
            policy->validate_relocation(function->target,
                operand->value.symbol.name,
                operand->value.symbol.relocation,
                operand->value.symbol.addend, policy->context);
    case MINIC_MACHINE_OPERAND_BLOCK:
        return operand->value.block_id < function->block_count;
    case MINIC_MACHINE_OPERAND_ADDRESS:
        address = &operand->value.address;
        return (!address->has_base || machine_validate_register(
                    function, policy, &address->base)) &&
            (!address->has_index || machine_validate_register(
                    function, policy, &address->index)) &&
            (!address->has_segment || machine_validate_register(
                    function, policy, &address->segment)) &&
            policy->validate_address != NULL &&
            policy->validate_address(function->target, address,
                                     policy->context);
    default:
        return false;
    }
}

static bool machine_validate_instruction(
    const MinicMachineFunction *function, const MinicMachineVerifyPolicy *policy,
    const MinicMachineInstruction *ins, bool *terminates,
    bool *indirect_successors) {
    size_t i;
    MinicMachineOpcodeContract contract = {0};
    if (ins->operand_count > MINIC_MACHINE_MAX_OPERANDS ||
        ins->implicit_count > MINIC_MACHINE_MAX_REGISTER_EFFECTS ||
        (ins->effects & ~MACHINE_VALID_EFFECTS) != 0U ||
        ((ins->effects & MINIC_MACHINE_EFFECT_VOLATILE) != 0U &&
         (ins->effects & (MINIC_MACHINE_EFFECT_MEMORY_READ |
                          MINIC_MACHINE_EFFECT_MEMORY_WRITE)) == 0U))
        return false;
    for (i = 0U; i < ins->operand_count; ++i)
        if (!machine_operand_valid(function, policy, &ins->operands[i]))
            return false;
    for (i = 0U; i < ins->implicit_count; ++i)
        if (!machine_validate_reg_effect(function, policy,
                                         &ins->implicit[i])) return false;
    if (ins->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM) {
        if (ins->opcode != 0U || ins->source_asm_template == NULL ||
            ins->source_asm_template[0] == '\0' ||
            (ins->effects & MINIC_MACHINE_EFFECT_SOURCE_ASM) == 0U ||
            policy->validate_source_asm == NULL ||
            !policy->validate_source_asm(
                function->target, ins, policy->context)) return false;
        for (i = 0U; i < ins->operand_count; ++i) {
            const MinicMachineOperand *operand = &ins->operands[i];
            if (operand->kind == MINIC_MACHINE_OPERAND_BLOCK) {
                if (operand->asm_constraint != NULL) return false;
            } else if (operand->asm_constraint == NULL ||
                       operand->asm_constraint[0] == '\0') return false;
        }
        *terminates = (ins->effects & MINIC_MACHINE_EFFECT_RETURN) != 0U;
        *indirect_successors = false;
        return true;
    }
    if (ins->kind != MINIC_MACHINE_INSTRUCTION_SELECTED ||
        ins->opcode == 0U || ins->source_asm_template != NULL ||
        (ins->effects & MINIC_MACHINE_EFFECT_SOURCE_ASM) != 0U ||
        policy->lookup_opcode == NULL ||
        !policy->lookup_opcode(function->target, ins->opcode,
                              &contract, policy->context) ||
        contract.operand_count > MINIC_MACHINE_MAX_OPERANDS ||
        contract.implicit_count > MINIC_MACHINE_MAX_REGISTER_EFFECTS ||
        ins->operand_count != contract.operand_count ||
        ins->implicit_count != contract.implicit_count ||
        (contract.required_generated_features &
             ~function->generated_features) != 0U ||
        (contract.required_effects & ~contract.permitted_effects) != 0U ||
        (contract.permitted_effects & ~MACHINE_VALID_EFFECTS) != 0U ||
        (ins->effects & contract.required_effects) !=
            contract.required_effects ||
        (ins->effects & ~contract.permitted_effects) != 0U ||
        ((ins->effects & MINIC_MACHINE_EFFECT_BRANCH) == 0U &&
         contract.has_indirect_successors) ||
        (contract.terminates_block &&
         (ins->effects & (MINIC_MACHINE_EFFECT_BRANCH |
                          MINIC_MACHINE_EFFECT_RETURN)) == 0U)) return false;
    for (i = 0U; i < ins->operand_count; ++i) {
        const MinicMachineOperand *operand = &ins->operands[i];
        if (operand->kind != contract.operand_kinds[i] ||
            operand->asm_constraint != NULL ||
            (operand->kind == MINIC_MACHINE_OPERAND_REGISTER &&
             operand->value.physical.access != contract.register_access[i]) ||
            (operand->kind != MINIC_MACHINE_OPERAND_REGISTER &&
             contract.register_access[i] != 0U)) return false;
    }
    for (i = 0U; i < ins->implicit_count; ++i) {
        if (ins->implicit[i].access != contract.implicit[i].access ||
            !machine_same_register(&ins->implicit[i].reg,
                                   &contract.implicit[i].reg)) return false;
    }
    *terminates = contract.terminates_block;
    *indirect_successors = contract.has_indirect_successors;
    return true;
}

static int machine_compare_labels(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

bool minic_machine_function_verify(const MinicMachineFunction *function,
                                   const MinicMachineVerifyPolicy *policy) {
    bool valid = false;
    const char **labels = NULL;
    size_t *referenced = NULL;
    size_t *visited = NULL;
    size_t label_count = 0U;
    size_t i;
    if (function == NULL || policy == NULL || function->target == NULL ||
        function->name == NULL ||
        !minic_core_assembler_symbol_lexically_safe(function->name) ||
        function->architecture != function->target->architecture ||
        function->architecture < MINIC_TARGET_ARCH_RISCV64 ||
        function->architecture > MINIC_TARGET_ARCH_RISCV32 ||
        function->block_count == 0U ||
        function->block_count > function->block_capacity ||
        function->blocks == NULL ||
        function->block_count > SIZE_MAX / sizeof(*labels) ||
        !machine_validate_frames(function)) return false;
    if ((function->architecture == MINIC_TARGET_ARCH_RISCV32 ||
         function->architecture == MINIC_TARGET_ARCH_RISCV64) &&
        function->target->riscv_isa.strict_isa &&
        function->generated_features !=
            (uint64_t)function->target->riscv_isa.codegen_features)
        return false;
    labels = calloc(function->block_count, sizeof(*labels));
    referenced = calloc(function->block_count, sizeof(*referenced));
    visited = calloc(function->block_count, sizeof(*visited));
    if (labels == NULL || referenced == NULL || visited == NULL) goto cleanup;
    for (i = 0U; i < function->block_count; ++i) {
        const MinicMachineBlock *block = &function->blocks[i];
        const size_t stamp = i + 1U;
        size_t unmatched_edges = 0U;
        size_t j;
        bool terminated = false;
        bool allow_opaque = false;
        if (block->id != i ||
            block->instruction_count > block->instruction_capacity ||
            (block->instruction_count != 0U &&
             block->instructions == NULL) ||
            block->successor_count > block->successor_capacity ||
            (block->successor_count != 0U &&
             block->successors == NULL) ||
            (block->has_fallthrough &&
             block->fallthrough_target >= function->block_count) ||
            (block->exported_label != NULL &&
             !minic_core_assembler_symbol_lexically_safe(
                 block->exported_label))) goto cleanup;
        if (block->exported_label != NULL)
            labels[label_count++] = block->exported_label;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicMachineInstruction *ins = &block->instructions[j];
            bool is_terminator = false;
            bool indirect = false;
            size_t operand_id;
            if (terminated || !machine_validate_instruction(
                    function, policy, ins, &is_terminator, &indirect))
                goto cleanup;
            if (is_terminator) {
                if (j + 1U != block->instruction_count) goto cleanup;
                terminated = true;
            }
            if (indirect) allow_opaque = true;
            for (operand_id = 0U; operand_id < ins->operand_count;
                 ++operand_id) {
                const MinicMachineOperand *operand = &ins->operands[operand_id];
                if (operand->kind == MINIC_MACHINE_OPERAND_BLOCK) {
                    if ((ins->effects & MINIC_MACHINE_EFFECT_BRANCH) == 0U)
                        goto cleanup;
                    if (referenced[operand->value.block_id] != stamp) {
                        referenced[operand->value.block_id] = stamp;
                        ++unmatched_edges;
                    }
                }
            }
        }
        if (!terminated && !block->has_fallthrough) goto cleanup;
        if (block->has_fallthrough) {
            if (block->fallthrough_target != i + 1U ||
                (terminated && block->instruction_count != 0U &&
                 (block->instructions[block->instruction_count - 1U].effects &
                  MINIC_MACHINE_EFFECT_RETURN) != 0U)) goto cleanup;
            if (referenced[block->fallthrough_target] != stamp) {
                referenced[block->fallthrough_target] = stamp;
                ++unmatched_edges;
            }
        }
        for (j = 0U; j < block->successor_count; ++j) {
            const size_t successor = block->successors[j];
            if (successor >= function->block_count ||
                visited[successor] == stamp) goto cleanup;
            visited[successor] = stamp;
            if (referenced[successor] != stamp &&
                !(allow_opaque && function->blocks[successor].indirect_entry))
                goto cleanup;
            if (referenced[successor] == stamp) --unmatched_edges;
        }
        if (unmatched_edges != 0U) goto cleanup;
    }
    qsort(labels, label_count, sizeof(*labels), machine_compare_labels);
    for (i = 1U; i < label_count; ++i)
        if (strcmp(labels[i - 1U], labels[i]) == 0) goto cleanup;
    valid = true;
cleanup:
    free(labels);
    free(referenced);
    free(visited);
    return valid;
}
