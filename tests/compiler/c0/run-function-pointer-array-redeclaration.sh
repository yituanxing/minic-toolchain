#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
good="$root/tests/compiler/c0/function_pointer_array_redeclaration.c"
bad="$root/tests/compiler/c0/invalid_function_pointer_array_redeclaration.c"

test -x "$minic" || { echo 'FUNC_ARRAY_REDECL=FAIL compiler missing' >&2; exit 1; }
"$host_cc" -std=gnu11 -Wall -Wextra -Werror \
    -Wno-array-parameter -no-pie "$good" -o "$work/gnu"
"$work/gnu"
"$host_cc" -E -P -std=gnu11 "$good" -o "$work/good.i"

for target in x86_64-linux aarch64-linux loongarch64-linux riscv64-linux riscv32-linux; do
    case "$target" in
        riscv32-linux) set -- -march=rv32i -mabi=ilp32 ;;
        *) set -- ;;
    esac
    MINIC_CORE_IR=strict "$minic" --target="$target" "$@" \
        -S "$work/good.i" -o "$work/$target.s" || {
        echo "FUNC_ARRAY_REDECL=FAIL positive target=$target" >&2
        exit 1
    }
    test -s "$work/$target.s"
done

"$host_cc" -no-pie "$work/x86_64-linux.s" -o "$work/minic"
"$work/minic"

for variant in BAD_BOUND BAD_ELEMENT_QUALIFIER BAD_NESTED_BOUND; do
    if "$host_cc" -std=gnu11 -Werror -fsyntax-only "-D$variant" "$bad" \
        >"$work/gnu-$variant.out" 2>"$work/gnu-$variant.err"; then
        echo "FUNC_ARRAY_REDECL=FAIL GNU accepted $variant" >&2
        exit 1
    fi
    "$host_cc" -E -P -std=gnu11 "-D$variant" "$bad" -o "$work/$variant.i"
    for target in x86_64-linux aarch64-linux loongarch64-linux riscv64-linux riscv32-linux; do
        case "$target" in
            riscv32-linux) set -- -march=rv32i -mabi=ilp32 ;;
            *) set -- ;;
        esac
        if "$minic" --target="$target" "$@" -S "$work/$variant.i" \
            -o "$work/$variant-$target.s" >"$work/$variant-$target.out" \
            2>"$work/$variant-$target.err"; then
            echo "FUNC_ARRAY_REDECL=FAIL MiniC admitted $variant target=$target" >&2
            exit 1
        fi
        test ! -e "$work/$variant-$target.s"
        grep -F 'conflicting function declaration' "$work/$variant-$target.err" >/dev/null || {
            sed -n '1,8p' "$work/$variant-$target.err" >&2
            echo "FUNC_ARRAY_REDECL=FAIL wrong diagnostic $variant target=$target" >&2
            exit 1
        }
    done
done

echo 'FUNC_ARRAY_REDECL=PASS targets=5 same_bound=1 incomplete_bound=1 nested=1 rejected=15 gcc_oracle=1 x86_native=1'
