#include "target/common/machine_gnu_asm.h"

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
    }
    return true;
}

bool minic_machine_gnu_asm_emit_body(FILE *output,
    const MinicMachineFunction *function,
    const MinicMachineGNUAsmPolicy *policy) {
    MachineOwnedLabels owned = {0};
    MinicMachineGNUAsmLabels view;
    size_t i;
    bool ok = false;
    if (output == NULL || function == NULL || policy == NULL ||
        policy->emit_selected == NULL ||
        !minic_machine_function_verify(function, &policy->verify))
        return false;
    /* Do all allocation and validation before writing the first byte. */
    if (!machine_owned_labels_initialize(&owned, function)) goto done;
    view.names = (const char *const *)owned.names;
    view.count = owned.count;
    /* Refuse a target that cannot emit *every* instruction kind before the
     * first label is written. A missing SourceASM serializer is a legitimate
     * fail-closed state while the complete source binding path is migrated. */
    if (policy->emit_source_asm == NULL) {
        for (i = 0U; i < function->block_count; ++i) {
            const MinicMachineBlock *block = &function->blocks[i];
            size_t j;
            for (j = 0U; j < block->instruction_count; ++j)
                if (block->instructions[j].kind ==
                    MINIC_MACHINE_INSTRUCTION_SOURCE_ASM) goto done;
        }
    }
    for (i = 0U; i < function->block_count; ++i) {
        const MinicMachineBlock *block = &function->blocks[i];
        size_t j;
        if ((i != 0U || block->exported_label != NULL) &&
            fprintf(output, "%s:\n", view.names[i]) < 0) goto done;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicMachineInstruction *instruction =
                &block->instructions[j];
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
