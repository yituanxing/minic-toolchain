#include "target/common/machine_ir_verify.h"
#include "core/symbol_identity.h"

#include <ctype.h>
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

static bool machine_source_binding_name_valid(const char *name) {
    const unsigned char *cursor = (const unsigned char *)name;
    if (cursor == NULL) return true;
    if (!(isalpha(*cursor) || *cursor == '_')) return false;
    for (++cursor; *cursor != '\0'; ++cursor)
        if (!(isalnum(*cursor) || *cursor == '_')) return false;
    return true;
}

static bool machine_validate_frames(const MinicMachineFunction *function,
    const MinicMachineVerifyPolicy *policy) {
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
            obj->base > MINIC_MACHINE_FRAME_BASE_ENTRY_STACK_POINTER ||
            (function->has_dynamic_stack &&
             obj->base == MINIC_MACHINE_FRAME_BASE_STACK_POINTER &&
             obj->area != MINIC_MACHINE_FRAME_OUTGOING) ||
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
            if (obj->fixed_offset < 0 &&
                obj->size_bytes > distance) return false;
        }
        if (!policy->validate_frame_object(function->target, function,
                                           obj, policy->context)) return false;
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
    case MINIC_MACHINE_OPERAND_BLOCK_ADDRESS:
        return operand->value.block_id < function->block_count &&
            function->blocks[operand->value.block_id].indirect_entry;
    case MINIC_MACHINE_OPERAND_CURRENT_LOCATION:
        return true;
    case MINIC_MACHINE_OPERAND_ADDRESS:
        address = &operand->value.address;
        return (!address->has_base || machine_validate_register(
                    function, policy, &address->base)) &&
            (!address->has_index || machine_validate_register(
                    function, policy, &address->index)) &&
            (!address->has_segment || machine_validate_register(
                    function, policy, &address->segment)) &&
            (address->has_symbol
                 ? (minic_core_assembler_symbol_lexically_safe(
                        address->symbol) &&
                    policy->validate_relocation != NULL &&
                    policy->validate_relocation(function->target,
                        address->symbol, address->relocation,
                        address->addend, policy->context))
                 : (address->symbol == NULL && address->relocation == 0U &&
                    address->addend == 0)) &&
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
    bool *indirect_successors, bool *permits_fallthrough) {
    size_t i;
    MinicMachineOpcodeContract contract = {0};
    if (ins->operand_count > MINIC_MACHINE_MAX_OPERANDS ||
        ins->implicit_count > MINIC_MACHINE_MAX_REGISTER_EFFECTS ||
        (ins->operand_count == 0U) != (ins->operands == NULL) ||
        (ins->implicit_count == 0U) != (ins->implicit == NULL) ||
        (ins->effects & ~MACHINE_VALID_EFFECTS) != 0U ||
        (ins->kind != MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
         (ins->effects & MINIC_MACHINE_EFFECT_VOLATILE) != 0U &&
         (ins->effects & (MINIC_MACHINE_EFFECT_MEMORY_READ |
                          MINIC_MACHINE_EFFECT_MEMORY_WRITE)) == 0U))
        return false;
    for (i = 0U; i < ins->operand_count; ++i)
        if (!machine_operand_valid(function, policy, &ins->operands[i]))
            return false;
    for (i = 0U; i < ins->implicit_count; ++i)
        if (!machine_validate_reg_effect(function, policy,
                                         &ins->implicit[i])) return false;
    if (!ins->has_core_origin &&
        (ins->core_block_id != 0U || ins->core_instruction_id != 0U))
        return false;
    if ((ins->kind != MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
         (ins->source_asm_machine_form_id != 0U ||
          ins->source_asm_machine_form_variant != 0U ||
          ins->has_source_asm_unique_id ||
          ins->source_asm_unique_id != 0U)) ||
        (ins->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
         ((ins->source_asm_machine_form_id == 0U &&
           ins->source_asm_machine_form_variant != 0U) ||
          (!ins->has_source_asm_unique_id &&
           ins->source_asm_unique_id != 0U)))) return false;
    if (ins->kind == MINIC_MACHINE_INSTRUCTION_LOCAL_LABEL) {
        if (ins->opcode != 0U || ins->effects != 0U ||
            ins->operand_count != 0U || ins->implicit_count != 0U ||
            ins->has_core_origin || ins->local_label_name == NULL ||
            !minic_core_assembler_symbol_lexically_safe(
                ins->local_label_name) || ins->source_asm_template != NULL ||
            ins->source_asm_length != 0U ||
            ins->source_asm_terminates_block ||
            ins->source_asm_permits_fallthrough ||
            ins->directive != NULL) return false;
        *terminates = false;
        *indirect_successors = false;
        *permits_fallthrough = false;
        return true;
    }
    if (ins->kind == MINIC_MACHINE_INSTRUCTION_DIRECTIVE) {
        const MinicMachineDirective *directive = ins->directive;
        const unsigned int valid_section_flags =
            MINIC_MACHINE_SECTION_ALLOC | MINIC_MACHINE_SECTION_WRITE |
            MINIC_MACHINE_SECTION_EXEC | MINIC_MACHINE_SECTION_LINK_ORDER;
        if (directive == NULL || ins->opcode != 0U || ins->effects != 0U ||
            ins->implicit_count != 0U || ins->source_asm_template != NULL ||
            ins->source_asm_length != 0U || ins->local_label_name != NULL ||
            ins->source_asm_terminates_block ||
            ins->source_asm_permits_fallthrough) return false;
        switch (directive->kind) {
        case MINIC_MACHINE_DIRECTIVE_PUSH_SECTION:
            if (ins->operand_count != 0U ||
                !minic_core_assembler_symbol_lexically_safe(
                    directive->section_name) ||
                (directive->section_flags & ~valid_section_flags) != 0U ||
                directive->section_type > MINIC_MACHINE_SECTION_TYPE_NOBITS ||
                directive->data_width_bytes != 0U ||
                (((directive->section_flags &
                   MINIC_MACHINE_SECTION_LINK_ORDER) != 0U) !=
                 (directive->link_order_symbol != NULL)) ||
                (directive->link_order_symbol != NULL &&
                 (!minic_core_assembler_symbol_lexically_safe(
                     directive->link_order_symbol) ||
                  directive->section_type ==
                      MINIC_MACHINE_SECTION_TYPE_NONE))) return false;
            break;
        case MINIC_MACHINE_DIRECTIVE_POP_SECTION:
            if (ins->operand_count != 0U || directive->section_name != NULL ||
                directive->section_flags != 0U ||
                directive->section_type != MINIC_MACHINE_SECTION_TYPE_NONE ||
                directive->link_order_symbol != NULL ||
                directive->data_width_bytes != 0U) return false;
            break;
        case MINIC_MACHINE_DIRECTIVE_DATA:
            if (directive->section_name != NULL ||
                directive->section_flags != 0U ||
                directive->section_type != MINIC_MACHINE_SECTION_TYPE_NONE ||
                directive->link_order_symbol != NULL ||
                (directive->data_width_bytes != 1U &&
                 directive->data_width_bytes != 2U &&
                 directive->data_width_bytes != 4U &&
                 directive->data_width_bytes != 8U) ||
                (ins->operand_count != 1U && ins->operand_count != 2U))
                return false;
            for (i = 0U; i < ins->operand_count; ++i) {
                const MinicMachineOperand *operand = &ins->operands[i];
                if ((operand->kind != MINIC_MACHINE_OPERAND_INTEGER &&
                     operand->kind != MINIC_MACHINE_OPERAND_SYMBOL &&
                     operand->kind != MINIC_MACHINE_OPERAND_BLOCK &&
                     operand->kind != MINIC_MACHINE_OPERAND_BLOCK_ADDRESS &&
                     operand->kind != MINIC_MACHINE_OPERAND_CURRENT_LOCATION) ||
                    operand->asm_constraint != NULL ||
                    operand->asm_name != NULL) return false;
            }
            break;
        default:
            return false;
        }
        *terminates = false;
        *indirect_successors = false;
        *permits_fallthrough = false;
        return true;
    }
    if (ins->kind == MINIC_MACHINE_INSTRUCTION_COMPILER_BARRIER) {
        if (ins->opcode != 0U ||
            ins->effects != MINIC_MACHINE_EFFECT_COMPILER_BARRIER ||
            ins->operand_count != 0U || ins->implicit_count != 0U ||
            !ins->has_core_origin || ins->source_asm_template != NULL ||
            ins->source_asm_length != 0U ||
            ins->source_asm_terminates_block ||
            ins->source_asm_permits_fallthrough ||
            ins->local_label_name != NULL || ins->directive != NULL)
            return false;
        *terminates = false;
        *indirect_successors = false;
        *permits_fallthrough = false;
        return true;
    }
    if (ins->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM) {
        if (ins->opcode != 0U || ins->source_asm_template == NULL ||
            ins->local_label_name != NULL || ins->directive != NULL ||
            ins->source_asm_length == SIZE_MAX ||
            !ins->has_core_origin ||
            ((ins->effects & MINIC_MACHINE_EFFECT_RETURN) != 0U &&
             !ins->source_asm_terminates_block) ||
            (ins->source_asm_terminates_block &&
             (ins->effects & (MINIC_MACHINE_EFFECT_BRANCH |
                              MINIC_MACHINE_EFFECT_RETURN)) == 0U) ||
            (ins->source_asm_permits_fallthrough &&
             (!ins->source_asm_terminates_block ||
              (ins->effects & MINIC_MACHINE_EFFECT_BRANCH) == 0U)) ||
            (ins->effects & MINIC_MACHINE_EFFECT_SOURCE_ASM) == 0U)
            return false;
        /* GNU asm("") with volatile/memory effects is a physical no-op,
         * not an ISA instruction or arbitrary assembler escape. Core must
         * preserve the compiler-side ordering even when no bytes print.
         * No target source-ASM translator is necessary for empty input. */
        if (ins->source_asm_length == 0U) {
            const unsigned int permitted = MINIC_MACHINE_EFFECT_SOURCE_ASM |
                MINIC_MACHINE_EFFECT_VOLATILE |
                MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
            if (ins->operand_count != 0U || ins->implicit_count != 0U ||
                ins->source_asm_machine_form_id != 0U ||
                ins->source_asm_machine_form_variant != 0U ||
                ins->source_asm_terminates_block ||
                ins->source_asm_permits_fallthrough ||
                (ins->effects & ~permitted) != 0U ||
                (ins->effects & (MINIC_MACHINE_EFFECT_VOLATILE |
                                 MINIC_MACHINE_EFFECT_COMPILER_BARRIER)) == 0U)
                return false;
            *terminates = false;
            *indirect_successors = false;
            *permits_fallthrough = false;
            return true;
        }
        if (policy->validate_source_asm == NULL ||
            !policy->validate_source_asm(
                function->target, ins, policy->context)) return false;
        for (i = 0U; i < ins->operand_count; ++i) {
            const MinicMachineOperand *operand = &ins->operands[i];
            size_t other;
            if (!machine_source_binding_name_valid(operand->asm_name))
                return false;
            if (operand->kind == MINIC_MACHINE_OPERAND_CURRENT_LOCATION)
                return false;
            if (operand->asm_name != NULL) {
                for (other = 0U; other < i; ++other)
                    if (ins->operands[other].asm_name != NULL &&
                        strcmp(operand->asm_name,
                               ins->operands[other].asm_name) == 0)
                        return false;
            }
            if (operand->kind == MINIC_MACHINE_OPERAND_BLOCK) {
                if (operand->asm_constraint != NULL) return false;
            } else if (operand->asm_constraint == NULL ||
                       operand->asm_constraint[0] == '\0') return false;
        }
        *terminates = ins->source_asm_terminates_block;
        *indirect_successors = false;
        *permits_fallthrough = ins->source_asm_permits_fallthrough;
        return true;
    }
    if (ins->kind != MINIC_MACHINE_INSTRUCTION_SELECTED ||
        ins->opcode == 0U || ins->source_asm_template != NULL ||
        ins->local_label_name != NULL || ins->directive != NULL ||
        ins->source_asm_length != 0U ||
        ins->source_asm_terminates_block ||
        ins->source_asm_permits_fallthrough ||
        (ins->effects & MINIC_MACHINE_EFFECT_SOURCE_ASM) != 0U ||
        policy->lookup_opcode == NULL ||
        policy->validate_selected_instruction == NULL ||
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
        (contract.permits_fallthrough &&
         (!contract.terminates_block ||
          (contract.required_effects & MINIC_MACHINE_EFFECT_BRANCH) == 0U)) ||
        (ins->effects & contract.required_effects) !=
            contract.required_effects ||
        (ins->effects & ~contract.permitted_effects) != 0U ||
        ((ins->effects & MINIC_MACHINE_EFFECT_BRANCH) == 0U &&
         contract.has_indirect_successors) ||
        ((ins->effects & MINIC_MACHINE_EFFECT_RETURN) != 0U &&
         !contract.terminates_block) ||
        ((ins->effects & MINIC_MACHINE_EFFECT_BRANCH) != 0U &&
         !contract.terminates_block) ||
        (contract.terminates_block &&
         (ins->effects & (MINIC_MACHINE_EFFECT_BRANCH |
                          MINIC_MACHINE_EFFECT_RETURN)) == 0U)) return false;
    for (i = 0U; i < ins->operand_count; ++i) {
        const MinicMachineOperand *operand = &ins->operands[i];
        if (operand->kind != contract.operand_kinds[i] ||
            operand->asm_constraint != NULL ||
            operand->asm_name != NULL ||
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
    if (!policy->validate_selected_instruction(
            function, ins, policy->context)) return false;
    *terminates = contract.terminates_block;
    *indirect_successors = contract.has_indirect_successors;
    *permits_fallthrough = contract.permits_fallthrough;
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
    uint64_t *source_unique_ids = NULL;
    size_t source_unique_count = 0U;
    size_t source_unique_capacity = 0U;
    size_t label_count = 0U;
    size_t label_capacity = 1U; /* the module-defined function symbol */
    size_t section_nesting = 0U;
    size_t i;
    if (function == NULL || policy == NULL || function->target == NULL ||
        function->target->data_layout == NULL ||
        function->target->data_layout->byte_order == MINIC_BYTE_ORDER_UNKNOWN ||
        !machine_power_of_two(function->target->data_layout->pointer_size) ||
        !machine_power_of_two(
            function->target->data_layout->pointer_alignment) ||
        function->name == NULL ||
        !minic_core_assembler_symbol_lexically_safe(function->name) ||
        function->architecture != function->target->architecture ||
        !minic_function_calling_convention_valid(
            function->calling_convention) ||
        policy->validate_function_abi == NULL ||
        !policy->validate_function_abi(
            function->target, function->calling_convention, policy->context) ||
        policy->validate_frame_object == NULL ||
        function->block_count == 0U ||
        function->block_count > function->block_capacity ||
        function->blocks == NULL ||
        function->block_count > SIZE_MAX / sizeof(*labels) ||
        policy->validate_generated_features == NULL ||
        !policy->validate_generated_features(function->target,
                                              function->generated_features,
                                              policy->context) ||
        !machine_validate_frames(function, policy)) return false;
    for (i = 0U; i < function->block_count; ++i) {
        const MinicMachineBlock *block = &function->blocks[i];
        size_t j;
        if (block->instruction_count > block->instruction_capacity ||
            (block->instruction_count != 0U && block->instructions == NULL) ||
            block->alias_count > block->alias_capacity ||
            (block->alias_count != 0U && block->alias_labels == NULL) ||
            block->alias_count >= SIZE_MAX - label_capacity)
            return false;
        label_capacity += block->alias_count + 1U;
        for (j = 0U; j < block->instruction_count; ++j) {
            if (block->instructions == NULL) return false;
            if (block->instructions[j].kind ==
                MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
                block->instructions[j].has_source_asm_unique_id) {
                if (source_unique_capacity == SIZE_MAX) return false;
                ++source_unique_capacity;
            }
            if (block->instructions[j].kind ==
                MINIC_MACHINE_INSTRUCTION_LOCAL_LABEL) {
                if (label_capacity == SIZE_MAX) return false;
                ++label_capacity;
            }
        }
    }
    if (label_capacity > SIZE_MAX / sizeof(*labels)) return false;
    labels = calloc(label_capacity, sizeof(*labels));
    referenced = calloc(function->block_count, sizeof(*referenced));
    visited = calloc(function->block_count, sizeof(*visited));
    if (source_unique_capacity >
        SIZE_MAX / sizeof(*source_unique_ids)) goto cleanup;
    source_unique_ids = calloc(source_unique_capacity == 0U
        ? 1U : source_unique_capacity, sizeof(*source_unique_ids));
    if (labels == NULL || referenced == NULL || visited == NULL ||
        source_unique_ids == NULL) goto cleanup;
    labels[label_count++] = function->name;
    for (i = 0U; i < function->block_count; ++i) {
        const MinicMachineBlock *block = &function->blocks[i];
        const size_t stamp = i + 1U;
        size_t unmatched_edges = 0U;
        size_t j;
        bool terminated = false;
        bool allow_opaque = false;
        bool terminal_allows_fallthrough = false;
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
                 block->exported_label)) ||
            (block->exported_label != NULL &&
             strcmp(block->exported_label, function->name) == 0))
            goto cleanup;
        if (block->exported_label != NULL)
            labels[label_count++] = block->exported_label;
        for (j = 0U; j < block->alias_count; ++j) {
            const char *alias = block->alias_labels[j];
            if (!minic_core_assembler_symbol_lexically_safe(alias))
                goto cleanup;
            labels[label_count++] = alias;
        }
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicMachineInstruction *ins = &block->instructions[j];
            bool is_terminator = false;
            bool indirect = false;
            bool permits_fallthrough = false;
            size_t operand_id;
            if ((terminated && ins->kind !=
                 MINIC_MACHINE_INSTRUCTION_DIRECTIVE &&
                 ins->kind != MINIC_MACHINE_INSTRUCTION_LOCAL_LABEL) ||
                !machine_validate_instruction(
                    function, policy, ins, &is_terminator, &indirect,
                    &permits_fallthrough))
                goto cleanup;
            if (ins->kind == MINIC_MACHINE_INSTRUCTION_DIRECTIVE) {
                if (ins->directive->kind ==
                    MINIC_MACHINE_DIRECTIVE_PUSH_SECTION) {
                    if (section_nesting == SIZE_MAX) goto cleanup;
                    ++section_nesting;
                } else if (ins->directive->kind ==
                    MINIC_MACHINE_DIRECTIVE_POP_SECTION) {
                    if (section_nesting == 0U) goto cleanup;
                    --section_nesting;
                }
            }
            if (ins->kind == MINIC_MACHINE_INSTRUCTION_LOCAL_LABEL)
                labels[label_count++] = ins->local_label_name;
            if (ins->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
                ins->has_source_asm_unique_id) {
                size_t earlier;
                for (earlier = 0U; earlier < source_unique_count; ++earlier)
                    if (source_unique_ids[earlier] ==
                        ins->source_asm_unique_id) goto cleanup;
                source_unique_ids[source_unique_count++] =
                    ins->source_asm_unique_id;
            }
            if (is_terminator) {
                terminated = true;
                terminal_allows_fallthrough = permits_fallthrough;
            }
            if (indirect) allow_opaque = true;
            for (operand_id = 0U; operand_id < ins->operand_count;
                 ++operand_id) {
                const MinicMachineOperand *operand = &ins->operands[operand_id];
                if (operand->kind == MINIC_MACHINE_OPERAND_BLOCK &&
                    ins->kind != MINIC_MACHINE_INSTRUCTION_DIRECTIVE) {
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
            if (terminated && !terminal_allows_fallthrough) goto cleanup;
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
    if (section_nesting != 0U) goto cleanup;
    qsort(labels, label_count, sizeof(*labels), machine_compare_labels);
    for (i = 1U; i < label_count; ++i)
        if (strcmp(labels[i - 1U], labels[i]) == 0) goto cleanup;
    valid = true;
cleanup:
    free(labels);
    free(referenced);
    free(visited);
    free(source_unique_ids);
    return valid;
}
