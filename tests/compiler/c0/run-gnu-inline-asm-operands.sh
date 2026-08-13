#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-operands
assembly="$work/gnu_inline_asm_operands.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_inline_asm_operands.c" \
    -o "$work/gnu_inline_asm_operands.i"
"$minic" -S "$work/gnu_inline_asm_operands.i" -o "$assembly"

test -s "$assembly"
grep -F 'amoadd.w zero, t1, (t0)' "$assembly" >/dev/null
grep -F 'amoadd.w t1, t3, (t0)' "$assembly" >/dev/null
grep -F 'addi t3, zero, 7' "$assembly" >/dev/null
grep -F 'add t0, t0, t3' "$assembly" >/dev/null
grep -F 'add t0, t1, t4' "$assembly" >/dev/null
grep -E '\.word \.Lminic_string_[0-9]+ - \.' "$assembly" >/dev/null
grep -F '.half 1262' "$assembly" >/dev/null
grep -F '.half 2313' "$assembly" >/dev/null
grep -F '.org 2b + 4' "$assembly" >/dev/null
grep -F 'add t0, zero, t1' "$assembly" >/dev/null
grep -F 'add t0, zero, zero' "$assembly" >/dev/null
grep -F 'csrs 0x100, 2' "$assembly" >/dev/null
grep -F 'csrs 0x100, t0' "$assembly" >/dev/null
matching_block=$(sed -n '/linux_matching_constraint_shape:/,/\.size linux_matching_constraint_shape/p' "$assembly")
printf '%s\n' "$matching_block" | grep -F 'ld t0, 0(sp)' >/dev/null
if printf '%s\n' "$matching_block" | grep -F 'ld t1,' >/dev/null; then
    echo 'FAIL compiler/c0/gnu_inline_asm_operands matching constraint allocated a second register' >&2
    exit 1
fi
if grep -E '\+A|"r"|=r' "$assembly" >/dev/null; then
    printf '%s\n' 'unexpected raw GNU asm constraints in emitted assembly' >&2
    exit 1
fi

cat >"$work/unsupported-clobber.c" <<'EOF'
int f(int value) {
    __asm__ __volatile__("add %0, %0, zero" : "+r"(value) : : "s1");
    return value;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/unsupported-clobber.c" -o "$work/unsupported-clobber.i"
if "$minic" -S "$work/unsupported-clobber.i" -o "$work/unsupported-clobber.s" \
    2>"$work/unsupported-clobber.stderr"; then
    printf '%s\n' 'unsupported register clobber unexpectedly accepted' >&2
    exit 1
fi
grep -F 'unsupported GNU asm register clobber for target' \
    "$work/unsupported-clobber.stderr" >/dev/null

cat >"$work/out-of-range-I.c" <<'EOF'
int f(int value) {
    __asm__ __volatile__("addi %0, %0, %1" : "+r"(value) : "I"(4096));
    return value;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/out-of-range-I.c" -o "$work/out-of-range-I.i"
if "$minic" -S "$work/out-of-range-I.i" -o "$work/out-of-range-I.s" \
    2>"$work/out-of-range-I.stderr"; then
    printf '%s\n' 'out-of-range I constraint unexpectedly accepted' >&2
    exit 1
fi
grep -F "GNU asm 'I' input requires a signed 12-bit integer constant" \
    "$work/out-of-range-I.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=r,+r inputs=r,I,i,rJ,rK,matching modifiers=z matching=same-register alternatives=rJ-zero+rK-u5 target=RV64'
