#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_SOURCE_ASM_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_SOURCE_ASM_H

#include "target/common/machine_gnu_asm.h"
#include "target/common/direct_block_context.h"
#include "target/common/machine_builder.h"

/* Exact opaque GNU source preflight for the live Core->Machine boundary. */
bool minic_loongarch64_qualified_opaque_asm(const MinicCoreInlineAsm *);

/* Target-owned SOURCE_ASM gateway. The source bytes are never compiler-
 * selected opcodes, and the serializer must repeat the verifier's parser. */
bool minic_loongarch64_machine_validate_source_asm(
    const MinicTargetInfo *, const MinicMachineInstruction *, void *);
bool minic_loongarch64_machine_emit_source_asm(
    FILE *, const MinicMachineFunction *, const MinicMachineInstruction *,
    const MinicMachineGNUAsmLabels *, void *);

/* Core-origin adapter: emits only forms with fully materialized machine
 * bindings. Unsupported register/memory transport fails closed. */
bool minic_loongarch64_select_core_source_asm(
    MinicMachineBuilder *, const MinicDirectLP64BlockContext *,
    const MinicCoreInstruction *);

/* Sealed GNU I-constraint `dbar %0`: lower the Core constant to a physical
 * MachineIR immediate, without carrying a C expression into the backend. */
bool minic_loongarch64_select_core_dbar_hint(
    MinicMachineBuilder *, const MinicDirectLP64BlockContext *,
    const MinicCoreInstruction *);

#endif
