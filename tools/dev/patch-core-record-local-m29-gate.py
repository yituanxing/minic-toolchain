#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M29 gate {label} anchor count={count}, expected 1")
    return text.replace(old, new, 1)


path = Path('.github/scripts/compiler-c0-full-gate.sh')
text = path.read_text()
function_anchor = '''core_inline_asm_identity_m28_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-inline-asm-identity-m28" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-inline-asm-identity-m28.sh
}

'''
function_insert = function_anchor + '''core_record_local_m29_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-record-local-m29" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-record-local-m29.sh
}

'''
text = replace_once(text, function_anchor, function_insert, 'function')
start_anchor = '''start_gate core-inline-asm-identity-m28-focused core_inline_asm_identity_m28_focused
'''
start_insert = start_anchor + '''start_gate core-record-local-m29-focused core_record_local_m29_focused
'''
text = replace_once(text, start_anchor, start_insert, 'start-gate')
path.write_text(text)
print('M29_GATE_PATCH_APPLIED')
