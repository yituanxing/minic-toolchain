#!/usr/bin/env python3
"""Isolated P16: remove exactly one redundant Core IR verification pass.

Invariant: successful Core Lowering verifies every lowered function via
minic_core_function_verify(); backend core_function_can_emit() verifies again
before machine code emission. The intermediate compiler.c validation only
inspects statuses and function emission policy, and no intervening pass
mutates the Core IR. Keep both earlier and later checks intact.
"""
from pathlib import Path

path = Path("src/compiler/compiler.c")
s = path.read_text()
old = """        status = set->statuses[function_index];
        if (status == MINIC_CORE_LOWER_OK &&
            !minic_core_function_verify(&set->functions[function_index])) {
            status = MINIC_CORE_LOWER_ERROR;
        }
        if (status == MINIC_CORE_LOWER_OK) {
"""
new = """        /* P16: the Core Lowering exit already verified this Core Function,
         * and RV64 core_function_can_emit() re-verifies before codegen.
         * Pruning in between only changes function emission references,
         * not the Core IR being verified. Keep status validation unchanged. */
        status = set->statuses[function_index];
        if (status == MINIC_CORE_LOWER_OK) {
"""
count = s.count(old)
if count != 1:
    raise SystemExit(f"CORE_VERIFY_DEDUP_ANCHOR_COUNT={count}, expected 1")
path.write_text(s.replace(old, new, 1))
print("MINIC_CORE_VERIFY_DEDUP_AUDIT_V1=APPLIED")
