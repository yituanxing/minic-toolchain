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
grep -F '.type memory_input_linux_shape, @function' "$assembly" >/dev/null
grep -F 'lw t1, 0(t3)' "$assembly" >/dev/null

cat >"$work/core-memory-input.c" <<'EOF'
static int core_memory_input_linux_shape(const int *value) {
    long error = 0;
    int loaded;

    __asm__ __volatile__("lw %1, %2" : "+r"(error), "=&r"(loaded) : "m"(*value));
    return loaded + (int)error;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/core-memory-input.c" \
    -o "$work/core-memory-input.i"
MINIC_CORE_IR=strict "$minic" -S "$work/core-memory-input.i" \
    -o "$work/core-memory-input.s"
grep -F '.type core_memory_input_linux_shape, @function' "$work/core-memory-input.s" >/dev/null
grep -F 'lw t1, (t2)' "$work/core-memory-input.s" >/dev/null

cat >"$work/core-generic-structured.c" <<'EOF'
static long output_three_inputs(long a, long b, long c) {
    long out;
    __asm__ __volatile__("add %0, %1, %2\n\txor %0, %0, %3"
                         : "=&r"(out) : "r"(a), "r"(b), "r"(c) : "memory");
    return out;
}

static void three_inputs_a0_clobber(long a, long b, long c) {
    __asm__ __volatile__("add t0, %0, %1\n\txor t0, t0, %2"
                         : : "r"(a), "r"(b), "r"(c) : "a0");
}

static long five_early_outputs(long seed) {
    long a = seed, b = seed, c = seed, d = seed, e = seed;
    __asm__ __volatile__("add %0, %2, %3\n\tadd %1, %4, zero"
                         : "=&r"(a), "=&r"(b), "+&r"(c), "+&r"(d), "+&r"(e)
                         : : "memory");
    return a + b + c + d + e;
}

static long mixed_atomic(long *p, long a, long b, long c, long d) {
    __asm__ __volatile__("amoadd.d %1, %3, %0\n\tadd %2, %2, %4"
                         : "+A"(*p), "+r"(a), "+r"(b) : "r"(c), "r"(d) : "memory");
    return a + b;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/core-generic-structured.c" \
    -o "$work/core-generic-structured.i"
MINIC_CORE_IR=strict "$minic" -S "$work/core-generic-structured.i" \
    -o "$work/core-generic-structured.s"
grep -F 'add t0, t3, t4' "$work/core-generic-structured.s" >/dev/null
grep -F 'xor t0, t0, t5' "$work/core-generic-structured.s" >/dev/null
grep -F 'add t0, t3, t4' "$work/core-generic-structured.s" >/dev/null
grep -F 'amoadd.d t0, t3, (t2)' "$work/core-generic-structured.s" >/dev/null

grep -F 'addi t3, zero, 7' "$assembly" >/dev/null
grep -F 'add t0, t0, t3' "$assembly" >/dev/null
grep -F 'add t0, t1, t4' "$assembly" >/dev/null
if grep -E '\+A|"r"|=r' "$assembly" >/dev/null; then
    printf '%s\n' 'unexpected raw GNU asm constraints in emitted assembly' >&2
    exit 1
fi

cat >"$work/argument-clobber.c" <<'EOF'
int f(int value) {
    __asm__ __volatile__("add %0, %0, zero" : "+r"(value) : : "a0", "a7");
    return value;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/argument-clobber.c" -o "$work/argument-clobber.i"
"$minic" -S "$work/argument-clobber.i" -o "$work/argument-clobber.s"
grep -F 'add t0, t0, zero' "$work/argument-clobber.s" >/dev/null

cat >"$work/callee-saved-fallback.c" <<'EOF'
long f(long left, long right) {
    long result;

    __asm__ __volatile__("add %0, %1, %2"
                         : "=r"(result)
                         : "r"(left), "r"(right)
                         : "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
                           "t0", "t1", "t2", "t3", "t4", "t5", "t6");
    return result;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/callee-saved-fallback.c" \
    -o "$work/callee-saved-fallback.i"
"$minic" -S "$work/callee-saved-fallback.i" -o "$work/callee-saved-fallback.s"
grep -F '  sd s1, 24(sp)' "$work/callee-saved-fallback.s" >/dev/null
grep -F '  sd s2, 32(sp)' "$work/callee-saved-fallback.s" >/dev/null
grep -F '  sd s3, 40(sp)' "$work/callee-saved-fallback.s" >/dev/null
grep -F 'add s1, s2, s3' "$work/callee-saved-fallback.s" >/dev/null
grep -F '  ld s1, 24(sp)' "$work/callee-saved-fallback.s" >/dev/null
grep -F '  ld s2, 32(sp)' "$work/callee-saved-fallback.s" >/dev/null
grep -F '  ld s3, 40(sp)' "$work/callee-saved-fallback.s" >/dev/null

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
    'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=m,=r,+r register-lvalue=local+member inputs=r,rJ,I,m memory-input=lvalue clobber=memory,t3 reservation=t3->t4 callee-saved-fallback=s1-s3 immediates=rv64-I placeholders=0,1,2 staging=stack target=RV64'
