#!/usr/bin/env bash
# Static production-wiring inventory for the five Core -> MachineIR targets.
# Runtime execution and complete Core/Machine effect coverage are certified by
# probe_machine_ir.py and check-machine-production-preflight, not by grep.
#
# Default mode reports remaining legacy code without claiming final cutover.
# --final additionally requires removal of obsolete direct-emit entrypoints.
set -Eeuo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$root"

mode=${1:-current}
case "$mode" in
    current|--final) ;;
    *) printf 'usage: %s [current|--final]\n' "$0" >&2; exit 2 ;;
esac

fail() { printf 'MACHINE_PRODUCTION_SEAL=FAIL %s\n' "$*" >&2; exit 1; }
need() {
    [[ -f "$1" ]] || fail "missing $1"
    LC_ALL=C grep -Fq -- "$2" "$1" || fail "$1 lacks $2"
}
forbid() {
    if LC_ALL=C grep -Fq -- "$2" "$1"; then
        fail "$1 retains $2"
    fi
}

# Every public architecture dispatch entry is selected from a single catalog.
# Only the sealed Core function/module and TargetInfo may enter the backend.
backend=src/target/backend.c
need "$backend" '#include "target/target_registration.def"'
need "$backend" 'minic_backend_emit_assembly_with_module('
need "$backend" 'minic_program_emit_module_preflight('
for arch in riscv32 riscv64 x86_64 aarch64 loongarch64; do
    need "$backend" "static const MinicBackend ${arch}_backend"
done
need "$backend" '&id##_backend'
forbid "$backend" 'MinicC0Program'

# A function must be COMPLETELY selected, checked against Core effects and
# physical ISA/ABI, then serialized via the ONE typed GNU emitter. These are
# production writer files, never standalone MachineIR demonstration probes.
for file in src/target/riscv32/rv32_emit.c \
            src/target/riscv64/codegen_function.c \
            src/target/x86_64/codegen.c; do
    need "$file" 'minic_machine_production_preflight_prepare('
    need "$file" 'minic_machine_gnu_asm_emit_prepared_body('
    forbid "$file" 'MinicC0Program'
done
need src/target/riscv32/rv32_emit.c 'minic_riscv_machine_select_core_subset('
need src/target/riscv64/codegen_function.c 'minic_riscv_machine_select_core_subset('
need src/target/x86_64/codegen.c 'minic_x86_64_machine_select_core_subset('
need src/target/common/direct_function_emit.h 'minic_machine_production_preflight_prepare('
need src/target/common/direct_function_emit.h 'minic_machine_gnu_asm_emit_prepared_body('
need src/target/common/machine_production_preflight.h 'minic_machine_production_preflight('
need src/target/common/machine_gnu_asm.c 'minic_machine_gnu_asm_emit_body('
for target in aarch64 loongarch64; do
    file="src/target/$target/codegen.c"
    need "$file" '.select_machine = minic_'
    need "$file" '.initialize_machine_asm_policy ='
    need "$file" 'minic_multi_direct_lp64_emit_function('
    forbid "$file" 'MinicC0Program'
done

# The Core and machine input contracts do not contain the source language's
# type arena or Program graph. Source-specific admission may still live in
# *source* policies; that is the intended upstream ownership boundary.
for file in src/core/core_ir.h src/core/core_type.h \
            src/target/common/machine_ir.h \
            src/target/common/machine_production_preflight.h; do
    forbid "$file" 'MinicC0Program'
    forbid "$file" 'MinicType '
done
need src/target/common/machine_core_coverage.c 'machine_core_edges_match('
need src/target/common/machine_core_coverage.c 'seen_source_asm'

if [[ "$mode" == '--final' ]]; then
    # Seal all RV64 ABI modes. The default LP64D dispatch must not re-enter
    # the historical direct Core->FILE* emitter after the typed MIR cutover.
    forbid src/target/riscv64/codegen_function.c \
        'minic_riscv64_emit_core_function_with_symbol_with_storage_module_trusted('
    # Direct LP64 cannot keep a selectable FILE* instruction path even if the
    # two production target registrations happen to choose the typed branch.
    forbid src/target/common/direct_function_emit.h 'emit_core_blocks'
    forbid src/target/common/direct_function_emit.h 'emit_prologue'
    # A converted x86 function may not retain an unreachable direct emitter
    # behind a goto. Merely calling the MachineIR emitter once proves nothing
    # while the old native instruction loop remains in the TU.
    forbid src/target/x86_64/codegen.c 'x86_emit_core_instruction('
    forbid src/target/x86_64/codegen.c 'x86_emit_prologue('
    # These files contain obsolete direct instruction generation, not source
    # qualification or ABI declarations. Physical forms must move to target
    # MachineIR selectors/opcode contracts before their final removal.
    for file in src/target/x86_64/machine_emit.c \
                src/target/aarch64/block_emit.c \
                src/target/aarch64/residual_emit.c; do
        [[ ! -e "$file" ]] || fail "legacy production file remains: $file"
    done
fi

printf 'MACHINE_PRODUCTION_SEAL=PASS mode=%s backends=5 selected=5 preflight=5 common_gnu_emitter=1\n' "$mode"
