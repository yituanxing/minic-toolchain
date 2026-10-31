#!/usr/bin/env bash
set -Eeuo pipefail

minic=${1:?expected MiniC-cc executable}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

targets=(x86_64-linux aarch64-linux loongarch64-linux riscv32-linux riscv64-linux)
positive=(
    gnu_static_multidimensional_pointer_arrays
    gnu_static_union_anonymous_record_designator
    gnu_static_union_anonymous_record_arena_growth
    gnu_local_function_pointer_noreturn_attribute
    gnu_local_nonstring_array_attribute
)
negative=(
    invalid_static_multidimensional_pointer_scalar
    invalid_static_union_distinct_active_member
    invalid_local_scalar_noreturn_attribute
    invalid_local_nonstring_attribute_arguments
)

for name in "${positive[@]}"; do
    source="$root/tests/compiler/c0/$name.c"
    gcc -E -P -x c "$source" -o "$work/$name.i"
    gcc -std=gnu11 -Wall -Wextra -Werror -O0 -no-pie "$source" \
        -o "$work/$name.gnu"
    "$work/$name.gnu"
    for target in "${targets[@]}"; do
        "$minic" --target="$target" -S "$work/$name.i" \
            -o "$work/$name.$target.s"
        test -s "$work/$name.$target.s"
    done
    gcc -no-pie -x assembler "$work/$name.x86_64-linux.s" \
        -o "$work/$name.minic" 2>"$work/$name.as.err"
    "$work/$name.minic"
done

for name in "${negative[@]}"; do
    source="$root/tests/compiler/c0/$name.c"
    gcc -E -P -x c "$source" -o "$work/$name.i"
    if gcc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$source" \
        >"$work/$name.gnu.out" 2>"$work/$name.gnu.err"; then
        echo "GNU unexpectedly accepted invalid initializer/attribute: $name" >&2
        exit 1
    fi
    for target in "${targets[@]}"; do
        if "$minic" --target="$target" -S "$work/$name.i" \
            -o "$work/$name.$target.s" >"$work/$name.$target.out" \
            2>"$work/$name.$target.err"; then
            echo "MiniC $target unexpectedly accepted invalid form: $name" >&2
            exit 1
        fi
        test ! -s "$work/$name.$target.s"
    done
done

echo 'MINIC_GNU_LINUX_FRONTEND_INITIALIZERS=PASS source_families=5 five_isa_positive=25 five_isa_negative=20 gnu_native=5 minic_x86_native=5 gnu_negative=4 union_arena_stability=1'
