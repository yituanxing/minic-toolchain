#include "target/common/machine_gnu_asm.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct MachineOwnedLabels {
    char **names;
    size_t count;
} MachineOwnedLabels;

static void machine_owned_labels_destroy(MachineOwnedLabels *labels) {
    size_t i;
    if (labels == NULL) return;
    for (i = 0U; i < labels->count; ++i) free(labels->names[i]);
    free(labels->names);
    labels->names = NULL;
    labels->count = 0U;
}

static char *machine_label_copy(const char *name) {
    size_t len;
    char *owned;
    if (name == NULL) return NULL;
    len = strlen(name);
    if (len == SIZE_MAX) return NULL;
    owned = malloc(len + 1U);
    if (owned != NULL) memcpy(owned, name, len + 1U);
    return owned;
}

/* A local machine-block label belongs to the function and target-independent
 * machine CFG. GNU syntax and branches themselves remain target-owned. */
static char *machine_internal_label(const char *name, size_t block_id) {
    static const char prefix[] = ".Lminic_mir_";
    size_t digits = 1U, value = block_id;
    size_t len;
    char *owned;
    while (value >= 10U) { value /= 10U; ++digits; }
    len = strlen(name);
    if (len > SIZE_MAX - sizeof(prefix) - digits - 1U) return NULL;
    owned = malloc((sizeof(prefix) - 1U) + len + 1U + digits + 1U);
    if (owned == NULL) return NULL;
    if (snprintf(owned, (sizeof(prefix) - 1U) + len + 1U + digits + 1U,
                 "%s%s_%zu", prefix, name, block_id) < 0) {
        free(owned);
        return NULL;
    }
    return owned;
}

static bool machine_owned_labels_initialize(MachineOwnedLabels *labels,
    const MinicMachineFunction *function) {
    size_t i;
    if (function->block_count > SIZE_MAX / sizeof(*labels->names))
        return false;
    labels->names = calloc(function->block_count, sizeof(*labels->names));
    if (labels->names == NULL) return false;
    labels->count = function->block_count;
    for (i = 0U; i < function->block_count; ++i) {
        const char *exported = function->blocks[i].exported_label;
        size_t other;
        if (exported != NULL) {
            labels->names[i] = machine_label_copy(exported);
        } else if (i == 0U) {
            labels->names[i] = machine_label_copy(function->name);
        } else {
            labels->names[i] = machine_internal_label(function->name, i);
        }
        if (labels->names[i] == NULL) return false;
        for (other = 0U; other < i; ++other)
            if (strcmp(labels->names[i], labels->names[other]) == 0)
                return false;
        /* Generated names must not collide with an externally visible
         * Core/address-taken alias or a target-selected local marker. */
        for (other = 0U; other < function->block_count; ++other) {
            const MinicMachineBlock *block = &function->blocks[other];
            size_t j;
            for (j = 0U; j < block->alias_count; ++j)
                if (strcmp(labels->names[i], block->alias_labels[j]) == 0)
                    return false;
            for (j = 0U; j < block->instruction_count; ++j) {
                const MinicMachineInstruction *ins = &block->instructions[j];
                if (ins->kind == MINIC_MACHINE_INSTRUCTION_LOCAL_LABEL &&
                    strcmp(labels->names[i], ins->local_label_name) == 0)
                    return false;
            }
        }
    }
    return true;
}

static bool machine_emit_data_reference(FILE *output,
    const MinicMachineOperand *operand,
    const MinicMachineGNUAsmLabels *labels) {
    size_t id;
    switch (operand->kind) {
    case MINIC_MACHINE_OPERAND_INTEGER:
        return fprintf(output, "%" PRId64, operand->value.integer) >= 0;
    case MINIC_MACHINE_OPERAND_SYMBOL:
        if (fprintf(output, "%s", operand->value.symbol.name) < 0)
            return false;
        if (operand->value.symbol.addend != 0)
            return fprintf(output, "%+" PRId64,
                           operand->value.symbol.addend) >= 0;
        return true;
    case MINIC_MACHINE_OPERAND_BLOCK:
    case MINIC_MACHINE_OPERAND_BLOCK_ADDRESS:
        id = operand->value.block_id;
        return id < labels->count &&
            fprintf(output, "%s", labels->names[id]) >= 0;
    case MINIC_MACHINE_OPERAND_CURRENT_LOCATION:
        return fputc('.', output) != EOF;
    default:
        return false;
    }
}

static bool machine_emit_directive(FILE *output,
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels) {
    const MinicMachineDirective *directive = instruction->directive;
    if (directive == NULL) return false;
    if (directive->kind == MINIC_MACHINE_DIRECTIVE_PUSH_SECTION) {
        char flags[5];
        size_t count = 0U;
        char type_marker = function->architecture == MINIC_TARGET_ARCH_AARCH64
            ? '%' : '@';
        if (directive->section_flags & MINIC_MACHINE_SECTION_ALLOC)
            flags[count++] = 'a';
        if (directive->section_flags & MINIC_MACHINE_SECTION_WRITE)
            flags[count++] = 'w';
        if (directive->section_flags & MINIC_MACHINE_SECTION_EXEC)
            flags[count++] = 'x';
        if (directive->section_flags & MINIC_MACHINE_SECTION_LINK_ORDER)
            flags[count++] = 'o';
        flags[count] = '\0';
        if (fprintf(output, "\t.pushsection %s,\"%s\"",
                    directive->section_name, flags) < 0) return false;
        if (directive->section_type != MINIC_MACHINE_SECTION_TYPE_NONE &&
            fprintf(output, ",%c%s", type_marker,
                directive->section_type == MINIC_MACHINE_SECTION_TYPE_NOBITS
                    ? "nobits" : "progbits") < 0) return false;
        if (directive->link_order_symbol != NULL &&
            fprintf(output, ",%s", directive->link_order_symbol) < 0)
            return false;
        return fputc('\n', output) != EOF;
    }
    if (directive->kind == MINIC_MACHINE_DIRECTIVE_POP_SECTION)
        return fputs("\t.popsection\n", output) != EOF;
    if (directive->kind == MINIC_MACHINE_DIRECTIVE_DATA) {
        const char *name = directive->data_width_bytes == 1U ? "byte" :
            directive->data_width_bytes == 2U ? "short" :
            directive->data_width_bytes == 4U ? "long" : "quad";
        if (fprintf(output, "\t.%s ", name) < 0 ||
            !machine_emit_data_reference(output, &instruction->operands[0],
                                         labels)) return false;
        if (instruction->operand_count == 2U &&
            (fputs(" - ", output) == EOF ||
             !machine_emit_data_reference(output, &instruction->operands[1],
                                          labels))) return false;
        return fputc('\n', output) != EOF;
    }
    return false;
}

bool minic_machine_gnu_asm_emit_unbound_source_asm(FILE *output,
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    size_t i;
    const char *bytes;
    (void)function;
    (void)labels;
    (void)context;
    if (output == NULL || instruction == NULL ||
        instruction->kind != MINIC_MACHINE_INSTRUCTION_SOURCE_ASM ||
        instruction->operand_count != 0U ||
        instruction->source_asm_template == NULL) return false;
    if (instruction->source_asm_length == 0U) return true;
    bytes = instruction->source_asm_template;
    for (i = 0U; i < instruction->source_asm_length; ++i)
        if (bytes[i] == '\0' || bytes[i] == '%') return false;
    if (fwrite(bytes, 1U, instruction->source_asm_length, output) !=
        instruction->source_asm_length) return false;
    return bytes[instruction->source_asm_length - 1U] == '\n' ||
        fputc('\n', output) != EOF;
}

static bool machine_gnu_asm_prepare(const MinicMachineFunction *function,
    const MinicMachineGNUAsmPolicy *policy, MachineOwnedLabels *owned) {
    size_t i;
    if (function == NULL || policy == NULL || policy->emit_selected == NULL ||
        !minic_machine_function_verify(function, &policy->verify) ||
        !machine_owned_labels_initialize(owned, function)) return false;
    if (policy->emit_source_asm == NULL) {
        for (i = 0U; i < function->block_count; ++i) {
            const MinicMachineBlock *block = &function->blocks[i];
            size_t j;
            for (j = 0U; j < block->instruction_count; ++j)
                if (block->instructions[j].kind ==
                    MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
                    block->instructions[j].source_asm_length != 0U)
                    return false;
        }
    }
    return true;
}

bool minic_machine_gnu_asm_preflight(
    const MinicMachineFunction *function,
    const MinicMachineGNUAsmPolicy *policy) {
    MachineOwnedLabels owned = {0};
    bool ok = machine_gnu_asm_prepare(function, policy, &owned);
    machine_owned_labels_destroy(&owned);
    return ok;
}

bool minic_machine_gnu_asm_emit_body(FILE *output,
    const MinicMachineFunction *function,
    const MinicMachineGNUAsmPolicy *policy) {
    MachineOwnedLabels owned = {0};
    MinicMachineGNUAsmLabels view;
    size_t i;
    bool ok = false;
    if (output == NULL || !machine_gnu_asm_prepare(function, policy, &owned))
        goto done;
    view.names = (const char *const *)owned.names;
    view.count = owned.count;
    for (i = 0U; i < function->block_count; ++i) {
        const MinicMachineBlock *block = &function->blocks[i];
        size_t j;
        if ((i != 0U || block->exported_label != NULL) &&
            fprintf(output, "%s:\n", view.names[i]) < 0) goto done;
        for (j = 0U; j < block->alias_count; ++j)
            if (fprintf(output, "%s:\n", block->alias_labels[j]) < 0)
                goto done;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicMachineInstruction *instruction =
                &block->instructions[j];
            /* Effect-only source ASM remains in MIR for scheduling/barrier
             * correctness but produces no target instruction text. */
            if (instruction->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
                instruction->source_asm_length == 0U) continue;
            if (instruction->kind == MINIC_MACHINE_INSTRUCTION_LOCAL_LABEL) {
                if (fprintf(output, "%s:\n",
                            instruction->local_label_name) < 0) goto done;
                continue;
            }
            if (instruction->kind == MINIC_MACHINE_INSTRUCTION_DIRECTIVE) {
                if (!machine_emit_directive(output, function, instruction,
                                            &view)) goto done;
                continue;
            }
            MinicMachineGNUAsmEmitInstruction emitter =
                instruction->kind == MINIC_MACHINE_INSTRUCTION_SELECTED
                ? policy->emit_selected : policy->emit_source_asm;
            if (emitter == NULL || !emitter(output, function, instruction,
                                            &view, policy->context)) goto done;
        }
    }
    ok = !ferror(output);
done:
    machine_owned_labels_destroy(&owned);
    return ok;
}
