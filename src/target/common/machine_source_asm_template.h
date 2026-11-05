#ifndef MINIC_TARGET_COMMON_MACHINE_SOURCE_ASM_TEMPLATE_H
#define MINIC_TARGET_COMMON_MACHINE_SOURCE_ASM_TEMPLATE_H

#include "target/common/machine_gnu_asm.h"

/* GNU extended-ASM placeholder grammar is shared across all targets.  An ISA
 * supplies exact admitted machine forms and the concrete physical operand
 * formatter; Common never parses register names or generates target syntax. */
typedef struct MinicMachineSourceAsmTemplatePolicy {
    bool (*validate_form)(const MinicMachineFunction *,
                          const MinicMachineInstruction *, void *);
    /* Called for EVERY binding, including unused bindings. The target owns
     * constraint/register/address legality and read/write effects. */
    bool (*validate_binding)(const MinicMachineFunction *,
                             const MinicMachineInstruction *, size_t, void *);
    /* Called first with output=NULL for validation and again with output
     * non-NULL to print. A modifier of 0 means ordinary %0/%[name]; 'l'
     * denotes a goto label. All other single-letter modifiers (c/n/z,
     * x86 w/b/k etc.) are interpreted exclusively by the target. */
    bool (*format_operand)(FILE *, const MinicMachineFunction *,
                           const MinicMachineInstruction *,
                           const MinicMachineGNUAsmLabels *,
                           size_t, unsigned char, void *);
    void *context;
} MinicMachineSourceAsmTemplatePolicy;

/* Validate a nonempty machine-admitted SOURCE_ASM instruction without
 * emitting text. Output labels may be NULL until GNU emission assigns them. */
bool minic_machine_source_asm_template_preflight(
    const MinicMachineFunction *, const MinicMachineInstruction *,
    const MinicMachineSourceAsmTemplatePolicy *);

/* GNU substitution into an existing target-owned section/output. This runs
 * the SAME dry-run grammar before writing anything and does not add a final
 * newline or invent an assembler directive. */
bool minic_machine_source_asm_template_emit(
    FILE *, const MinicMachineFunction *, const MinicMachineInstruction *,
    const MinicMachineGNUAsmLabels *,
    const MinicMachineSourceAsmTemplatePolicy *);

#endif
