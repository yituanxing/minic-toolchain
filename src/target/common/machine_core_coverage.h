#ifndef MINIC_TARGET_COMMON_MACHINE_CORE_COVERAGE_H
#define MINIC_TARGET_COMMON_MACHINE_CORE_COVERAGE_H

#include "core/core_ir.h"
#include "target/common/machine_ir.h"

/* A production Core->Machine selector must preserve every executable Core
 * effect.  Pure SSA definitions may fold into target operands, but observable
 * memory/call/ASM/ABI effects need provenance and the corresponding physical
 * effect bits.  This is an additional check beyond physical MachineVerifier;
 * neither check makes a partial target selector production-complete alone. */
bool minic_machine_core_coverage_verify(const MinicCoreFunction *,
                                        const bool *reachable_blocks,
                                        const MinicMachineFunction *);

#endif
