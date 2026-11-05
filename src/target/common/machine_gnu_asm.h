#ifndef MINIC_TARGET_COMMON_MACHINE_GNU_ASM_H
#define MINIC_TARGET_COMMON_MACHINE_GNU_ASM_H

#include "target/common/machine_ir_verify.h"

#include <stdio.h>

/* A verified machine-body serializer. Target-selected instructions are only
 * printable through target-owned opcode encoders, never through a stored
 * assembly line. The ELF/module owner remains responsible for section,
 * visibility, function symbol and size directives around this body. */
typedef struct MinicMachineGNUAsmLabels {
    /* The entry label names the enclosing function symbol. All other blocks
     * have a unique, stable assembler label, even if they are only reachable
     * from branches emitted by machine-internal instruction lowering. */
    const char *const *names;
    size_t count;
} MinicMachineGNUAsmLabels;

typedef bool (*MinicMachineGNUAsmEmitInstruction)(
    FILE *output, const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, void *context);

typedef struct MinicMachineGNUAsmPolicy {
    /* Target policies must independently recognize the selected opcode,
     * generated ISA features, all machine operands and implicit effects. */
    MinicMachineVerifyPolicy verify;
    MinicMachineGNUAsmEmitInstruction emit_selected;
    /* Source-owned inline ASM has its own validated grammar and provenance.
     * A target without a complete source binding emitter leaves this NULL,
     * causing a closed failure rather than printed, unverified text. */
    MinicMachineGNUAsmEmitInstruction emit_source_asm;
    void *context;
} MinicMachineGNUAsmPolicy;

/* The Module/ELF writer calls this before emitting the section/global/type
 * envelope. It verifies all machine content, including outlined fragments,
 * label uniqueness and availability of source-owned ASM serializers. */
bool minic_machine_gnu_asm_preflight(const MinicMachineFunction *function,
    const MinicMachineGNUAsmPolicy *policy);

/* Safe reusable common case for source-owned, operand-free inline ASM with
 * no GNU % substitution. Target validate_source_asm must still independently
 * admit this source template and all its effects. */
bool minic_machine_gnu_asm_emit_unbound_source_asm(FILE *output,
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, void *context);

/* Emits the body only after the WHOLE function has passed verification.
 * The caller must not emit the ELF envelope before it has committed to this
 * same verification gate, as a failed body may leave the output partial. */
bool minic_machine_gnu_asm_emit_body(FILE *output,
    const MinicMachineFunction *function,
    const MinicMachineGNUAsmPolicy *policy);

#endif
