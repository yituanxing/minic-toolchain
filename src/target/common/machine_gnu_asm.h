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

/* GAS ELF section type sigil is an assembler dialect policy, never a
 * Common-side architecture switch. The zero value preserves GNU @ for
 * standalone policy builders; targets assign their exact sigil explicitly. */
typedef enum MinicMachineGNUSectionTypeMarker {
    MINIC_MACHINE_GNU_SECTION_TYPE_AT = 0,
    MINIC_MACHINE_GNU_SECTION_TYPE_PERCENT
} MinicMachineGNUSectionTypeMarker;

typedef struct MinicMachineGNUAsmPolicy {
    MinicMachineGNUSectionTypeMarker section_type_marker;
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

/* A sealed, owned emission plan. Preparation validates the complete physical
 * MachineFunction and owns its label spelling exactly once, before any ELF
 * envelope is printed. The MachineFunction and target context must remain
 * alive and unchanged through emission. The opaque plan cannot be forged by
 * ordinary target callers or used as a substitute for Core coverage. */
typedef struct MinicMachineGNUAsmPrepared MinicMachineGNUAsmPrepared;

bool minic_machine_gnu_asm_prepare_body(
    const MinicMachineFunction *function,
    const MinicMachineGNUAsmPolicy *policy,
    MinicMachineGNUAsmPrepared **prepared);
bool minic_machine_gnu_asm_emit_prepared_body(
    FILE *output, const MinicMachineGNUAsmPrepared *prepared);
void minic_machine_gnu_asm_prepared_destroy(
    MinicMachineGNUAsmPrepared *prepared);

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
