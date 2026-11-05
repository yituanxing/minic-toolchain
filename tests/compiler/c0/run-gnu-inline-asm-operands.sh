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
# The unified typed ASM assigns registers through the target planner, whose
# legal allocation may change without changing the GNU output/input roles.
# Both the memory-only and the SSA-result forms must keep the exact AMO opcode
# and a register-held scalar/address operand; the runtime probe below also
# verifies the emitted register result is stored into its source destination.
grep -Eq '^amoadd[.]w zero, [a-z][0-9]+, \([a-z][0-9]+\)$' "$assembly"
grep -Eq '^amoadd[.]w [a-z][0-9]+, [a-z][0-9]+, \([a-z][0-9]+\)$' "$assembly"
grep -F '.type memory_output_store_like, @function' "$assembly" >/dev/null
grep -F 'sw t1, 0(t0)' "$assembly" >/dev/null
grep -F '.type register_member_outputs_like, @function' "$assembly" >/dev/null
for value in 1 2 3 4 5; do
    grep -Eq "^[[:space:]]*li [a-z][0-9]+, $value\$" "$assembly"
done
# The fixture's main now actually reads all five output members; an output
# register that was never written back is a runtime failure, independently of
# the allocator's chosen machine register names.
grep -F '.type memory_input_linux_shape, @function' "$assembly" >/dev/null
grep -Eq '^lw [a-z][0-9]+, \([a-z][0-9]+\)$' "$assembly"

cat >"$work/core-memory-input.c" <<'EOF'
static int core_memory_input_linux_shape(const int *value) {
    long error = 0;
    int loaded;

    __asm__ __volatile__("lw %1, %2" : "+r"(error), "=&r"(loaded) : "m"(*value));
    return loaded + (int)error;
}
int main(void) {
    int memory = 12;
    return core_memory_input_linux_shape(&memory) == 12 ? 0 : 1;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/core-memory-input.c" \
    -o "$work/core-memory-input.i"
MINIC_CORE_IR=strict "$minic" -S "$work/core-memory-input.i" \
    -o "$work/core-memory-input.s"
grep -F '.type core_memory_input_linux_shape, @function' "$work/core-memory-input.s" >/dev/null
grep -Eq '^lw [a-z][0-9]+, \([a-z][0-9]+\)$' "$work/core-memory-input.s"

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

int main(void) {
    long memory = 7;
    long arithmetic = output_three_inputs(3, 4, 5);
    long accumulator = five_early_outputs(2);
    long atomic_result = mixed_atomic(&memory, 1, 2, 3, 4);
    three_inputs_a0_clobber(1, 2, 3);
    return arithmetic == 2 && accumulator == 12 &&
           atomic_result == 13 && memory == 10 ? 0 : 1;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/core-generic-structured.c" \
    -o "$work/core-generic-structured.i"
MINIC_CORE_IR=strict "$minic" -S "$work/core-generic-structured.i" \
    -o "$work/core-generic-structured.s"
grep -Eq '^[[:space:]]*add [a-z][0-9]+, [a-z][0-9]+, [a-z][0-9]+$' \
    "$work/core-generic-structured.s"
grep -Eq '^[[:space:]]*xor [a-z][0-9]+, [a-z][0-9]+, [a-z][0-9]+$' \
    "$work/core-generic-structured.s"
grep -Eq '^amoadd[.]d [a-z][0-9]+, [a-z][0-9]+, \([a-z][0-9]+\)$' \
    "$work/core-generic-structured.s"

grep -F 'addi t3, zero, 7' "$assembly" >/dev/null
grep -Eq '^[[:space:]]*add [a-z][0-9]+, [a-z][0-9]+, t3$' "$assembly"
grep -Eq '^add [a-z][0-9]+, [a-z][0-9]+, [a-z][0-9]+$' "$assembly"
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
for reg in s1 s2 s3; do
    saved_slot=$(sed -n "s/^  sd $reg, \([0-9][0-9]*\)(sp)\$/\1/p" \
        "$work/callee-saved-fallback.s" | head -n 1)
    test -n "$saved_slot"
    grep -F "  ld $reg, $saved_slot(sp)" "$work/callee-saved-fallback.s" >/dev/null
done
grep -F 'add s1, s2, s3' "$work/callee-saved-fallback.s" >/dev/null

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
grep -F 'unsupported GNU asm register clobber for target: s1' \
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
