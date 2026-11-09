#!/usr/bin/env bash
# A specialized static pointer symbol remains a GNU `i` immediate at the
# ASM callsite even if a distinct `+r` output aliases and escapes the source
# pointer parameter. Output effects must not erase the proven input fact
# before Core materializes the emitted ASM relocation.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$repo_root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
source="$repo_root/tests/compiler/c0/p44_inline_asm_mutating_symbolic.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'P44_MUTATING_SYMBOLIC_ASM=FAIL %s\n' "$*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC executable missing'
"$host_cc" -std=gnu11 -E -P -x c "$source" -o "$work/input.i" \
    || fail 'GNU preprocessing'
"$host_cc" -std=gnu11 -O2 -Wall -Wextra -Werror -fno-pie \
    -S "$source" -o "$work/gnu.s" || fail 'GNU O2 assembly'
"$host_cc" -std=gnu11 -O2 -Wall -Wextra -Werror -fno-pie \
    -c "$source" -o "$work/gnu.o" || fail 'GNU ELF object'
"$host_cc" -no-pie "$work/gnu.o" -o "$work/gnu" \
    || fail 'GNU native link'
"$work/gnu" || fail 'GNU native result'
gnu_count=$(grep -Ec '^[[:space:]]*\.quad[[:space:]]+p44_mutating_symbolic_slots([[:space:]]|$)' \
    "$work/gnu.s" || true)
[[ "$gnu_count" -eq 1 ]] || fail "GNU O2 expected one symbolic .quad, got $gnu_count"
readelf -r "$work/gnu.o" >"$work/gnu.relocs"
gnu_elf=$(grep -c 'R_X86_64_64' "$work/gnu.relocs" || true)
[[ "$gnu_elf" -eq 1 ]] || fail "GNU ELF expected one 64-bit symbol relocation, got $gnu_elf"
nm -a "$work/gnu.o" | grep -Eq ' [a-zA-Z] p44_mutating_symbolic_slots$' \
    || fail 'GNU ELF lost static object identity'

targets=(riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux)
admitted=0
unsupported=0
native_minic=0
for target in "${targets[@]}"; do
    if "$minic" --target="$target" -S "$work/input.i" \
            -o "$work/$target.s" >"$work/$target.out" \
            2>"$work/$target.err"; then
        ((admitted += 1))
        count=$(grep -Ec '^[[:space:]]*\.quad[[:space:]]+p44_mutating_symbolic_slots([[:space:]]|$)' \
            "$work/$target.s" || true)
        [[ "$count" -eq 1 ]] \
            || fail "target=$target expected one GNU i relocation, got $count"
        grep -Fxq 'p44_mutating_symbolic_slots:' "$work/$target.s" \
            || fail "target=$target lost static object definition"
        if [[ "$target" == x86_64-linux ]]; then
            "$host_cc" -no-pie "$work/$target.s" -o "$work/minic-x86" \
                || fail 'MiniC x86 GNU AS/LD'
            "$work/minic-x86" || fail 'MiniC x86 native result'
            native_minic=1
        fi
    elif [[ "$target" == riscv32-linux || "$target" == riscv64-linux ]]; then
        tail -n 20 "$work/$target.err" >&2
        fail "mandatory target=$target failed Core GNU symbolic immediate"
    elif grep -Eiq 'target.*(unsupported|unclaimed)|Core instruction unsupported|Core instruction/ABI not yet supported by M1 emitter' \
            "$work/$target.err"; then
        ((unsupported += 1))
        printf 'P44_MUTATING_SYMBOLIC_FORM=SKIP target=%s reason=unadmitted-machine-asm-form\n' \
            "$target"
    else
        tail -n 20 "$work/$target.err" >&2
        fail "target=$target unexpected Core lowering failure"
    fi
done

printf 'P44_MUTATING_SYMBOLIC_ASM=PASS GNU_O2=1 GNU_ELF=1 GNU_native=0 RV_mandatory=2 admitted=%d unsupported=%d minic_x86_native=%d pointer_output_escape=safe static_symbol=retained\n' \
    "$admitted" "$unsupported" "$native_minic"
