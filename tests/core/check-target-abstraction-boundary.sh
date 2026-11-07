#!/usr/bin/env bash
set -Eeuo pipefail

host_cc=${1:-cc}
root=$(cd "$(dirname "$0")/../.." && pwd)
frontend_sources=("$root"/src/frontend/*.[ch])
compiler_sources=("$root"/src/compiler/*.[ch] "$root"/src/compiler/c_lower/*.[ch])
driver_sources=("$root"/tools/minic/*.[ch])
all_sources=("${frontend_sources[@]}" "${compiler_sources[@]}" "${driver_sources[@]}")

# The target catalog owns architecture identities. Driver may compare catalog
# identities for consistency, but these three consumers must never enumerate
# concrete target cases. A new ISA is installed through TargetInfo/DataLayout.
for source in "${all_sources[@]}"; do
    if LC_ALL=C grep -En \
        'MINIC_TARGET_ARCH_[A-Z0-9_]+|MINIC_ARCHITECTURE_[A-Z0-9_]+|MINIC_MACHINE_ARCH_[A-Z0-9_]+' \
        "$source"; then
        printf 'TARGET_ABSTRACTION_BOUNDARY=FAIL architecture_enum=%s\n' "$source" >&2
        exit 1
    fi
done

# Only the driver resolves names and stage recipes. Frontend and the Core
# construction pipeline receive an already selected TargetInfo invocation.
for source in "${frontend_sources[@]}" "${compiler_sources[@]}"; do
    if LC_ALL=C grep -En \
        'minic_target_catalog_(all|find|default)|minic_target_driver_stage_recipe_for_architecture' \
        "$source"; then
        printf 'TARGET_ABSTRACTION_BOUNDARY=FAIL catalog_selection=%s\n' "$source" >&2
        exit 1
    fi
done

# Transitive machine-target headers must not leak into Frontend, Compiler or
# Driver, including through an innocuous-looking new shared header. The
# generic target catalog and RISC-V feature *descriptor* are allowed; a
# target-specific emission/ABI implementation is not.
count=0
for source in "$root"/src/frontend/*.c "$root"/src/compiler/*.c \
              "$root"/src/compiler/c_lower/*.c "$root"/tools/minic/*.c; do
    dependencies=$("$host_cc" -std=c11 -MM -I"$root/include" -I"$root/src" "$source")
    case "$dependencies" in
        *'/target/riscv32/'*|*'/target/riscv64/'*|*'/target/x86_64/'*|\
        *'/target/aarch64/'*|*'/target/loongarch64/'*)
            printf 'TARGET_ABSTRACTION_BOUNDARY=FAIL target_include_closure=%s\n' "$source" >&2
            exit 1 ;;
    esac
    count=$((count + 1))
done

printf 'TARGET_ABSTRACTION_BOUNDARY=PASS translation_units=%s architecture_cases=0 target_specific_headers=0\n' "$count"
