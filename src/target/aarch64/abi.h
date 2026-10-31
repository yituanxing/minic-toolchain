#ifndef MINIC_TARGET_AARCH64_ABI_H
#define MINIC_TARGET_AARCH64_ABI_H
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
/* AAPCS64 >16-byte indirect aggregate gate. AAPCS64 HFA/HVA
 * classification takes precedence; until FP/vector aggregate
 * marshaling exists we fail closed rather than send a GP pointer. */
bool minic_aarch64_indirect_record(const MinicC0Program *,
                                   const MinicTargetInfo *, MinicType, size_t *);
#endif
