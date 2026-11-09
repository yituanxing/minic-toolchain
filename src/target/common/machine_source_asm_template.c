#include "target/common/machine_source_asm_template.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

enum { MINIC_MACHINE_SOURCE_ASM_TEMPLATE_MAX_BYTES = 1048576U };

static bool source_name_first(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool source_name_next(unsigned char c) {
    return source_name_first(c) || (c >= '0' && c <= '9');
}

static bool source_name_valid(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    if (p == NULL) return true;
    if (!source_name_first(*p++)) return false;
    while (*p != '\0')
        if (!source_name_next(*p++)) return false;
    return true;
}

static bool source_template_prepare(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineSourceAsmTemplatePolicy *policy) {
    size_t i;
    if (function == NULL || function->target == NULL ||
        instruction == NULL || policy == NULL ||
        policy->validate_form == NULL ||
        policy->validate_binding == NULL ||
        policy->format_operand == NULL ||
        instruction->kind != MINIC_MACHINE_INSTRUCTION_SOURCE_ASM ||
        instruction->source_asm_template == NULL ||
        instruction->source_asm_length >
            MINIC_MACHINE_SOURCE_ASM_TEMPLATE_MAX_BYTES ||
        instruction->operand_count > MINIC_MACHINE_MAX_OPERANDS ||
        ((instruction->operand_count == 0U) !=
         (instruction->operands == NULL)) ||
        (instruction->source_asm_machine_form_id == 0U &&
         instruction->source_asm_machine_form_variant != 0U) ||
        (!instruction->has_source_asm_unique_id &&
         instruction->source_asm_unique_id != 0U) ||
        !policy->validate_form(function, instruction, policy->context))
        return false;
    for (i = 0U; i < instruction->operand_count; ++i) {
        const MinicMachineOperand *binding = &instruction->operands[i];
        size_t prior;
        if (!source_name_valid(binding->asm_name) ||
            !policy->validate_binding(function, instruction, i,
                                      policy->context)) return false;
        for (prior = 0U; prior < i; ++prior)
            if (binding->asm_name != NULL &&
                instruction->operands[prior].asm_name != NULL &&
                strcmp(binding->asm_name,
                       instruction->operands[prior].asm_name) == 0)
                return false;
    }
    return true;
}

static bool source_template_lookup_named(
    const MinicMachineInstruction *instruction,
    const char *name, size_t length, size_t *operand_id) {
    size_t i, found = SIZE_MAX;
    for (i = 0U; i < instruction->operand_count; ++i) {
        const char *candidate = instruction->operands[i].asm_name;
        if (candidate != NULL && strlen(candidate) == length &&
            memcmp(candidate, name, length) == 0) {
            if (found != SIZE_MAX) return false;
            found = i;
        }
    }
    if (found == SIZE_MAX) return false;
    *operand_id = found;
    return true;
}

static bool source_template_walk(FILE *output,
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels,
    const MinicMachineSourceAsmTemplatePolicy *policy,
    bool require_label_binding) {
    size_t i;
    if (!source_template_prepare(function, instruction, policy))
        return false;
    for (i = 0U; i < instruction->source_asm_length; ++i) {
        const unsigned char ch =
            (unsigned char)instruction->source_asm_template[i];
        if (ch == '\0' || ch == '\r') return false;
        if (ch != '%') {
            if (output != NULL && fputc(ch, output) == EOF) return false;
            continue;
        }
        if (++i >= instruction->source_asm_length) return false;
        if (instruction->source_asm_template[i] == '%') {
            if (output != NULL && fputc('%', output) == EOF) return false;
            continue;
        }
        if (instruction->source_asm_template[i] == '=') {
            if (!instruction->has_source_asm_unique_id ||
                instruction->source_asm_unique_id == 0U) return false;
            if (output != NULL && fprintf(output, "%" PRIu64,
                instruction->source_asm_unique_id) < 0) return false;
            continue;
        }
        {
            unsigned char modifier = 0U;
            size_t id = SIZE_MAX;
            const unsigned char first =
                (unsigned char)instruction->source_asm_template[i];
            /* Recognize only the GNU syntax; the target decides whether a
             * modifier means a legal operation on its physical operand. */
            if ((first >= 'A' && first <= 'Z') ||
                (first >= 'a' && first <= 'z')) {
                modifier = first;
                if (++i >= instruction->source_asm_length) return false;
            }
            if (instruction->source_asm_template[i] == '[') {
                const size_t start = i + 1U;
                size_t end = start;
                while (end < instruction->source_asm_length &&
                       instruction->source_asm_template[end] != ']') ++end;
                if (end == instruction->source_asm_length || end == start ||
                    !source_template_lookup_named(instruction,
                        instruction->source_asm_template + start,
                        end - start, &id)) return false;
                i = end;
            } else if (instruction->source_asm_template[i] >= '0' &&
                       instruction->source_asm_template[i] <= '9') {
                id = 0U;
                do {
                    const unsigned digit =
                        (unsigned)(instruction->source_asm_template[i] - '0');
                    if (id > (SIZE_MAX - digit) / 10U) return false;
                    id = id * 10U + digit;
                    if (i + 1U == instruction->source_asm_length ||
                        instruction->source_asm_template[i + 1U] < '0' ||
                        instruction->source_asm_template[i + 1U] > '9') break;
                    ++i;
                } while (true);
            } else return false;
            if (id >= instruction->operand_count ||
                (modifier == 'l' &&
                 (instruction->operands[id].kind !=
                      MINIC_MACHINE_OPERAND_BLOCK ||
                  !instruction->source_asm_terminates_block ||
                  (instruction->effects & MINIC_MACHINE_EFFECT_BRANCH) == 0U ||
                  (require_label_binding &&
                   (labels == NULL || labels->names == NULL ||
                    instruction->operands[id].value.block_id >=
                        labels->count ||
                    labels->names[instruction->operands[id].value.block_id] ==
                        NULL)))) ||
                !policy->format_operand(output, function, instruction, labels,
                    id, modifier, policy->context)) return false;
        }
    }
    return true;
}

bool minic_machine_source_asm_template_preflight(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineSourceAsmTemplatePolicy *policy) {
    return source_template_walk(NULL, function, instruction, NULL, policy,
                                false);
}

bool minic_machine_source_asm_template_emit(
    FILE *output, const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels,
    const MinicMachineSourceAsmTemplatePolicy *policy) {
    return output != NULL &&
        source_template_walk(NULL, function, instruction, labels, policy,
                             true) &&
        source_template_walk(output, function, instruction, labels, policy,
                             true);
}
