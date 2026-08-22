#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M30 gate {label} anchor count={count}, expected 1")
    return text.replace(old, new, 1)


path = Path('.github/scripts/compiler-c0-full-gate.sh')
text = path.read_text()
function_anchor = '''core_record_local_m29_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-record-local-m29" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-record-local-m29.sh
}

'''
function_insert = function_anchor + '''core_aggregate_boundary_m30_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-aggregate-boundary-m30" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-aggregate-boundary-m30.sh
}

'''
text = replace_once(text, function_anchor, function_insert, 'function')
start_anchor = 'start_gate core-record-local-m29-focused core_record_local_m29_focused\n'
start_insert = start_anchor + 'start_gate core-aggregate-boundary-m30-focused core_aggregate_boundary_m30_focused\n'
text = replace_once(text, start_anchor, start_insert, 'start-gate')
path.write_text(text)
print('M30_GATE_PATCH_APPLIED')
