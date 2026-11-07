#include "target/common/machine_ir.h"

#include <stdlib.h>
#include <string.h>

static char *machine_copy_string(const char *text) {
    size_t length;
    char *copy;
    if (text == NULL) return NULL;
    length = strlen(text);
    if (length == SIZE_MAX) return NULL;
    copy = malloc(length + 1U);
    if (copy != NULL) memcpy(copy, text, length + 1U);
    return copy;
}

static char *machine_copy_bytes(const char *bytes, size_t length) {
    char *copy;
    if (bytes == NULL || length == SIZE_MAX) return NULL;
    copy = malloc(length + 1U);
    if (copy != NULL) {
        memcpy(copy, bytes, length);
        copy[length] = '\0';
    }
    return copy;
}

static bool machine_reserve(void **data, size_t *capacity,
                            size_t count, size_t element_size) {
    size_t new_capacity;
    void *grown;
    if (count <= *capacity) return true;
    if (element_size == 0U || count > SIZE_MAX / element_size) return false;
    new_capacity = *capacity == 0U ? 4U : *capacity;
    while (new_capacity < count) {
        if (new_capacity > SIZE_MAX / 2U) {
            new_capacity = count;
            break;
        }
        new_capacity *= 2U;
    }
    if (new_capacity > SIZE_MAX / element_size) return false;
    grown = realloc(*data, new_capacity * element_size);
    if (grown == NULL) return false;
    *data = grown;
    *capacity = new_capacity;
    return true;
}

static void machine_instruction_destroy(MinicMachineInstruction *ins) {
    size_t i;
    if (ins == NULL) return;
    for (i = 0U; ins->operands != NULL && i < ins->operand_count &&
                 i < MINIC_MACHINE_MAX_OPERANDS; ++i) {
        MinicMachineOperand *operand = &ins->operands[i];
        if (operand->kind == MINIC_MACHINE_OPERAND_SYMBOL)
            free(operand->value.symbol.name);
        if (operand->kind == MINIC_MACHINE_OPERAND_ADDRESS)
            free(operand->value.address.symbol);
        free(operand->asm_constraint);
        free(operand->asm_name);
    }
    free(ins->operands);
    free(ins->implicit);
    free(ins->source_asm_template);
    free(ins->local_label_name);
    if (ins->directive != NULL) {
        free(ins->directive->section_name);
        free(ins->directive->link_order_symbol);
        free(ins->directive);
    }
    memset(ins, 0, sizeof(*ins));
}

bool minic_machine_block_erase_instruction(MinicMachineFunction *function,
    size_t block_id, size_t instruction_id) {
    MinicMachineBlock *block;
    if (function == NULL || block_id >= function->block_count) return false;
    block = &function->blocks[block_id];
    if (instruction_id >= block->instruction_count) return false;
    machine_instruction_destroy(&block->instructions[instruction_id]);
    if (instruction_id + 1U < block->instruction_count)
        memmove(&block->instructions[instruction_id],
            &block->instructions[instruction_id + 1U],
            (block->instruction_count - instruction_id - 1U) *
                sizeof(*block->instructions));
    --block->instruction_count;
    memset(&block->instructions[block->instruction_count], 0,
        sizeof(*block->instructions));
    return true;
}

void minic_machine_function_destroy(MinicMachineFunction *function) {
    size_t block_id;
    if (function == NULL) return;
    for (block_id = 0U; block_id < function->block_count; ++block_id) {
        MinicMachineBlock *block = &function->blocks[block_id];
        size_t instruction_id;
        for (instruction_id = 0U;
             instruction_id < block->instruction_count; ++instruction_id)
            machine_instruction_destroy(&block->instructions[instruction_id]);
        free(block->instructions);
        free(block->successors);
        free(block->exported_label);
        for (instruction_id = 0U; instruction_id < block->alias_count;
             ++instruction_id) free(block->alias_labels[instruction_id]);
        free(block->alias_labels);
    }
    free(function->blocks);
    free(function->frame_objects);
    free(function->name);
    memset(function, 0, sizeof(*function));
}

bool minic_machine_function_initialize(MinicMachineFunction *function,
    const MinicTargetInfo *target, const char *name,
    uint64_t generated_features) {
    if (function == NULL || target == NULL || name == NULL || *name == '\0' ||
        function->name != NULL || function->blocks != NULL ||
        function->frame_objects != NULL) return false;
    function->name = machine_copy_string(name);
    if (function->name == NULL) return false;
    function->target = target;
    function->architecture = target->architecture;
    function->generated_features = generated_features;
    function->frame_alignment = 1U;
    return true;
}

bool minic_machine_function_add_frame_object(MinicMachineFunction *function,
    const MinicMachineFrameObject *object, size_t *id) {
    MinicMachineFrameObject *slot;
    if (function == NULL || function->name == NULL || object == NULL ||
        id == NULL || function->frame_object_count == SIZE_MAX ||
        !machine_reserve((void **)&function->frame_objects,
                         &function->frame_object_capacity,
                         function->frame_object_count + 1U,
                         sizeof(*function->frame_objects))) return false;
    *id = function->frame_object_count;
    slot = &function->frame_objects[function->frame_object_count++];
    *slot = *object;
    return true;
}

bool minic_machine_function_add_block(MinicMachineFunction *function,
    const char *exported_label, bool indirect_entry, size_t *id) {
    MinicMachineBlock *block;
    char *copied_label = NULL;
    if (function == NULL || function->name == NULL || id == NULL ||
        function->block_count == SIZE_MAX) return false;
    if (exported_label != NULL) {
        copied_label = machine_copy_string(exported_label);
        if (copied_label == NULL) return false;
    }
    if (!machine_reserve((void **)&function->blocks,
                         &function->block_capacity,
                         function->block_count + 1U,
                         sizeof(*function->blocks))) {
        free(copied_label);
        return false;
    }
    *id = function->block_count;
    block = &function->blocks[function->block_count++];
    memset(block, 0, sizeof(*block));
    block->id = *id;
    block->indirect_entry = indirect_entry;
    block->exported_label = copied_label;
    return true;
}

bool minic_machine_block_add_alias_label(MinicMachineFunction *function,
    size_t block_id, const char *alias_label) {
    MinicMachineBlock *block;
    char *copy;
    if (function == NULL || block_id >= function->block_count ||
        alias_label == NULL || alias_label[0] == '\0') return false;
    copy = machine_copy_string(alias_label);
    if (copy == NULL) return false;
    block = &function->blocks[block_id];
    if (block->alias_count == SIZE_MAX ||
        !machine_reserve((void **)&block->alias_labels,
                         &block->alias_capacity, block->alias_count + 1U,
                         sizeof(*block->alias_labels))) {
        free(copy);
        return false;
    }
    block->alias_labels[block->alias_count++] = copy;
    return true;
}

bool minic_machine_block_add_successor(MinicMachineFunction *function,
    size_t from, size_t to) {
    MinicMachineBlock *block;
    size_t i;
    if (function == NULL || from >= function->block_count ||
        to >= function->block_count) return false;
    block = &function->blocks[from];
    for (i = 0U; i < block->successor_count; ++i)
        if (block->successors[i] == to) return false;
    if (block->successor_count == SIZE_MAX ||
        !machine_reserve((void **)&block->successors,
                         &block->successor_capacity,
                         block->successor_count + 1U,
                         sizeof(*block->successors))) return false;
    block->successors[block->successor_count++] = to;
    return true;
}

bool minic_machine_block_set_fallthrough(MinicMachineFunction *function,
    size_t from, size_t to) {
    MinicMachineBlock *block;
    size_t i;
    if (function == NULL || from >= function->block_count ||
        to >= function->block_count) return false;
    block = &function->blocks[from];
    if (block->has_fallthrough) return false;
    for (i = 0U; i < block->successor_count; ++i)
        if (block->successors[i] == to) {
            block->has_fallthrough = true;
            block->fallthrough_target = to;
            return true;
        }
    return false;
}

bool minic_machine_block_append_instruction(MinicMachineFunction *function,
    size_t block_id, const MinicMachineInstruction *input) {
    MinicMachineBlock *block;
    MinicMachineInstruction copy;
    MinicMachineInstruction *slot;
    size_t i;
    if (function == NULL || block_id >= function->block_count ||
        input == NULL || input->operand_count > MINIC_MACHINE_MAX_OPERANDS ||
        input->implicit_count > MINIC_MACHINE_MAX_REGISTER_EFFECTS ||
        (input->operand_count == 0U) != (input->operands == NULL) ||
        (input->implicit_count == 0U) != (input->implicit == NULL) ||
        (input->source_asm_length != 0U &&
         input->source_asm_template == NULL))
        return false;
    /* Reject malformed nested relocation metadata before copying. Silently
     * clearing its unused symbol pointer would launder invalid source input
     * into a verifier-approved machine address. */
    for (i = 0U; i < input->operand_count; ++i) {
        const MinicMachineOperand *operand = &input->operands[i];
        if (operand->kind == MINIC_MACHINE_OPERAND_ADDRESS) {
            const MinicMachineAddress *address = &operand->value.address;
            if ((address->has_symbol && address->symbol == NULL) ||
                (!address->has_symbol &&
                 (address->symbol != NULL || address->relocation != 0U ||
                  address->addend != 0))) return false;
        }
    }
    memset(&copy, 0, sizeof(copy));
    copy = *input;
    copy.operands = NULL;
    copy.implicit = NULL;
    copy.source_asm_template = NULL;
    copy.local_label_name = NULL;
    copy.directive = NULL;
    if (input->operand_count != 0U) {
        copy.operands = calloc(input->operand_count, sizeof(*copy.operands));
        if (copy.operands == NULL) goto fail;
    }
    if (input->implicit_count != 0U) {
        if (input->implicit_count > SIZE_MAX / sizeof(*copy.implicit))
            goto fail;
        copy.implicit = malloc(input->implicit_count * sizeof(*copy.implicit));
        if (copy.implicit == NULL) goto fail;
        memcpy(copy.implicit, input->implicit,
               input->implicit_count * sizeof(*copy.implicit));
    }
    for (i = 0U; i < input->operand_count; ++i) {
        const MinicMachineOperand *source = &input->operands[i];
        MinicMachineOperand *destination = &copy.operands[i];
        *destination = *source;
        destination->asm_constraint = NULL;
        destination->asm_name = NULL;
        if (source->kind == MINIC_MACHINE_OPERAND_SYMBOL) {
            destination->value.symbol.name = NULL;
            if (source->value.symbol.name != NULL) {
                destination->value.symbol.name =
                    machine_copy_string(source->value.symbol.name);
                if (destination->value.symbol.name == NULL) goto fail;
            }
        } else if (source->kind == MINIC_MACHINE_OPERAND_ADDRESS) {
            destination->value.address.symbol = NULL;
            if (source->value.address.has_symbol &&
                source->value.address.symbol != NULL) {
                destination->value.address.symbol =
                    machine_copy_string(source->value.address.symbol);
                if (destination->value.address.symbol == NULL) goto fail;
            }
        }
        if (source->asm_constraint != NULL) {
            destination->asm_constraint =
                machine_copy_string(source->asm_constraint);
            if (destination->asm_constraint == NULL) goto fail;
        }
        if (source->asm_name != NULL) {
            destination->asm_name = machine_copy_string(source->asm_name);
            if (destination->asm_name == NULL) goto fail;
        }
    }
    if (input->source_asm_template != NULL) {
        copy.source_asm_template =
            machine_copy_bytes(input->source_asm_template,
                               input->source_asm_length);
        if (copy.source_asm_template == NULL) goto fail;
    }
    if (input->local_label_name != NULL) {
        copy.local_label_name = machine_copy_string(input->local_label_name);
        if (copy.local_label_name == NULL) goto fail;
    }
    if (input->directive != NULL) {
        copy.directive = calloc(1U, sizeof(*copy.directive));
        if (copy.directive == NULL) goto fail;
        *copy.directive = *input->directive;
        copy.directive->section_name = NULL;
        copy.directive->link_order_symbol = NULL;
        if (input->directive->section_name != NULL) {
            copy.directive->section_name = machine_copy_string(
                input->directive->section_name);
            if (copy.directive->section_name == NULL) goto fail;
        }
        if (input->directive->link_order_symbol != NULL) {
            copy.directive->link_order_symbol = machine_copy_string(
                input->directive->link_order_symbol);
            if (copy.directive->link_order_symbol == NULL) goto fail;
        }
    }
    block = &function->blocks[block_id];
    if (block->instruction_count == SIZE_MAX ||
        !machine_reserve((void **)&block->instructions,
                         &block->instruction_capacity,
                         block->instruction_count + 1U,
                         sizeof(*block->instructions))) goto fail;
    slot = &block->instructions[block->instruction_count++];
    *slot = copy;
    return true;
fail:
    machine_instruction_destroy(&copy);
    return false;
}
