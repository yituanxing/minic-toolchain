#!/usr/bin/env bash
# An inline-ASM immediate is a Module root only when its owning Core ASM
# instruction and CFG block survive. The GNU x86 object is the oracle for
# genuine relocation/ELF retention; the RV32/RV64 -S routes check MiniC's
# same GNU-preprocessed source. Other ISA structured-ASM admission is
# explicitly conditional, never silently counted as a passing target.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$repo_root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
source="$repo_root/tests/compiler/c0/p44_live_asm_reachability.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'P44_LIVE_ASM_ROOTS=FAIL %s\n' "$*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC executable missing'
"$host_cc" -std=gnu11 -E -P -x c "$source" -o "$work/asm.i" \
    || fail 'GNU preprocessing'
"$host_cc" -std=gnu11 -O2 -Wall -Wextra -Werror -fno-pie \
    -c "$source" -o "$work/gnu.o" || fail 'GNU -O2 object'
"$host_cc" -no-pie "$work/gnu.o" -o "$work/gnu" || fail 'GNU native link'
"$work/gnu" || fail 'GNU native return value'

nm -a "$work/gnu.o" >"$work/gnu.symbols"
if grep -Eq ' [a-zA-Z] p44_dead_(fn|object|branch_fn)$' \
        "$work/gnu.symbols"; then
    fail 'GNU -O2 unexpectedly retained dead ASM operand target'
fi
for name in p44_live_fn p44_live_object p44_live_bytes; do
    grep -Eq " [a-zA-Z] ${name}$" "$work/gnu.symbols" \
        || fail "GNU lost live ASM operand target=$name"
done
readelf -r "$work/gnu.o" >"$work/gnu.relocs"
relocs=$(grep -c 'R_X86_64_64' "$work/gnu.relocs" || true)
[[ "$relocs" -eq 4 ]] || fail "GNU inline ASM expected 4 ELF relocations, got $relocs"
for offset in 4 8; do
    grep -Eq "R_X86_64_64.*(\\.bss|p44_live_bytes) \\+ ${offset}$" \
        "$work/gnu.relocs" \
        || fail "GNU ELF lost nested byte-addend offset=$offset"
done

targets=(riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux)
admitted=0
unsupported=0
for target in "${targets[@]}"; do
    if "$minic" --target="$target" -S "$work/asm.i" \
            -o "$work/$target.s" >"$work/$target.out" \
            2>"$work/$target.err"; then
        ((admitted += 1))
        for name in p44_live_fn p44_live_object p44_live_bytes; do
            if [[ "$name" != p44_live_bytes ]]; then
                grep -Eq "^[[:space:]]*\\.quad[[:space:]]+${name}([[:space:]]|$)" \
                "$work/$target.s" \
                || fail "target=$target missing live GNU ASM relocation=$name"
            fi
            grep -Fxq "$name:" "$work/$target.s" \
                || fail "target=$target missing live owner=$name"
        done
        for offset in 4 8; do
            grep -Eq "^[[:space:]]*\\.quad[[:space:]]+p44_live_bytes\\+${offset}([[:space:]]|$)" \
                "$work/$target.s" \
                || fail "target=$target lost GNU immediate symbolic byte-addend=$offset"
        done
        if grep -Eq 'p44_dead_(fn|object|branch_fn|unresolved)' \
                "$work/$target.s"; then
            fail "target=$target emitted a dead GNU ASM operand root"
        fi
    elif [[ "$target" == riscv32-linux || "$target" == riscv64-linux ]]; then
        tail -n 18 "$work/$target.err" >&2
        fail "mandatory target=$target compile failed"
    elif grep -Eiq 'unsupported kind|target.*(unsupported|unclaimed)|Core instruction unsupported|Core instruction/ABI not yet supported by M1 emitter' \
            "$work/$target.err"; then
        ((unsupported += 1))
        printf 'P44_ASM_FORM_ADMISSION=SKIP target=%s reason=unadmitted-structured-immediate\n' \
            "$target"
    else
        tail -n 18 "$work/$target.err" >&2
        fail "target=$target unexpected rejection"
    fi
done

printf 'P44_LIVE_ASM_ROOTS=PASS same_preprocessed=1 GNU_x86_ELF=4 GNU_native=0 RV_mandatory=2 admitted=%d unsupported=%d dead_fn_object_cfg=pruned live_fn_object=retained nested_offsets=4,8\n' \
    "$admitted" "$unsupported"
