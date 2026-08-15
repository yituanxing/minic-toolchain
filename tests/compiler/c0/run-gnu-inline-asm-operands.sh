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
grep -F '.type memory_output_store_like, @function' "$assembly" >/dev/null
grep -F 'sw t1, 0(t0)' "$assembly" >/dev/null
grep -F '.type register_member_outputs_like, @function' "$assembly" >/dev/null
grep -F 'li t0, 1' "$assembly" >/dev/null
grep -F 'li t1, 2' "$assembly" >/dev/null
grep -F 'li t3, 3' "$assembly" >/dev/null
grep -F 'li t4, 4' "$assembly" >/dev/null
grep -F 'li t5, 5' "$assembly" >/dev/null
grep -F 'sd t5, 0(a0)' "$assembly" >/dev/null
grep -F 'addi t3, zero, 7' "$assembly" >/dev/null
grep -F 'add t0, t0, t3' "$assembly" >/dev/null
grep -F 'add t0, t1, t4' "$assembly" >/dev/null
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
    'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=m,=r,+r register-lvalue=local+member inputs=r,rJ,I clobber=memory,t3 reservation=t3->t4 immediates=rv64-I placeholders=0,1,2 staging=stack target=RV64'
