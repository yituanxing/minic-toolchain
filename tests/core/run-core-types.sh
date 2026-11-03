#!/usr/bin/env bash
set -Eeuo pipefail

host_cc=${1:-cc}
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

type_sources=(
    "$root/src/core/core_type.c"
    "$root/tests/core/core_type_context_test.c"
)
import_sources=(
    "$root/src/core/core_type.c"
    "$root/src/compiler/c_lower/core_type_import.c"
    "$root/src/frontend/ast.c"
    "$root/src/frontend/type.c"
    "$root/src/target/data_layout.c"
    "$root/src/target/source_policy/data_layout_source.c"
    "$root/src/target/target_info.c"
    "$root/src/target/target_catalog.c"
    "$root/src/target/source_policy/target_source_info.c"
    "$root/src/target/source_policy/language_semantics.c"
    "$root/src/target/source_policy/riscv32.c"
    "$root/src/target/source_policy/riscv64.c"
    "$root/src/target/source_policy/x86_64.c"
    "$root/src/target/source_policy/aarch64.c"
    "$root/src/target/source_policy/loongarch64.c"
    "$root/src/target/riscv32/machine_register.c"
    "$root/src/target/riscv64/machine_register.c"
    "$root/src/target/x86_64/machine_register.c"
    "$root/src/target/aarch64/machine_register.c"
    "$root/src/target/loongarch64/machine_register.c"
    "$root/tests/core/core_type_import_test.c"
)
snapshot_sources=(
    "$root/src/core/core_type.c"
    "$root/src/core/core_ir.c"
    "$root/src/compiler/c_lower/core_ir_source.c"
    "$root/src/core/core_ir_verify.c"
    "$root/src/frontend/type.c"
    "$root/tests/core/core_type_snapshot_test.c"
)
symbol_sources=(
    "$root/src/core/core_type.c"
    "$root/src/core/core_ir.c"
    "$root/src/core/core_ir_verify.c"
    "$root/tests/core/core_symbol_identity_test.c"
)

for mode in native asan; do
    extra=()
    if [ "$mode" = asan ]; then
        extra=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    else
        extra=(-O2)
    fi
    "$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -I"$root/src" -I"$root/include" "${extra[@]}" \
        "${type_sources[@]}" -o "$work/core-types-$mode"
    "$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -I"$root/src" -I"$root/include" "${extra[@]}" \
        "${import_sources[@]}" -o "$work/core-types-import-$mode"
    "$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -I"$root/src" -I"$root/include" "${extra[@]}" \
        "${snapshot_sources[@]}" -o "$work/core-types-snapshot-$mode"
    "$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -I"$root/src" -I"$root/include" "${extra[@]}" \
        "${symbol_sources[@]}" -o "$work/core-symbol-identity-$mode"
    # Under ptrace LeakSanitizer crashes independently of program correctness.
    # AddressSanitizer and UndefinedBehaviorSanitizer remain active.
    ASAN_OPTIONS=detect_leaks=0 "$work/core-types-$mode"
    ASAN_OPTIONS=detect_leaks=0 "$work/core-types-import-$mode"
    ASAN_OPTIONS=detect_leaks=0 "$work/core-types-snapshot-$mode"
    ASAN_OPTIONS=detect_leaks=0 "$work/core-symbol-identity-$mode"
done

echo 'CORE_TYPES=PASS tests=4 native=4 ASAN_UBSAN=4'
