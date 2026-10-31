#!/usr/bin/env bash
# Linux 6.6.143 arch/x86/lib/kaslr.c _ASM_MUL "%3" exact GNU operand
# roles. Test RDX:RAX high and low halves against GCC, including overflow.
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/mini.i" <<'EOF'
void mini_mul(unsigned long x, unsigned long y,
              unsigned long *low, unsigned long *high) {
    const unsigned long mix_const = y;
    unsigned long random = x, raw;
    asm("mulq %3" : "=a" (random), "=d" (raw)
                 : "a" (random), "rm" (mix_const));
    *low = random;
    *high = raw;
}
unsigned long mini_kaslr_const(unsigned long input) {
    const unsigned long mix_const = 0x5d6008cbf3848dd3UL;
    unsigned long raw, random = input;
    asm("mulq %3" : "=a"(random), "=d"(raw)
                  : "a"(random), "rm"(mix_const));
    return random + raw;
}
EOF
sed -e 's/mini_mul/gnu_mul/g' -e 's/mini_kaslr_const/gnu_kaslr_const/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
extern void mini_mul(unsigned long, unsigned long, unsigned long *, unsigned long *);
extern void gnu_mul(unsigned long, unsigned long, unsigned long *, unsigned long *);
extern unsigned long mini_kaslr_const(unsigned long), gnu_kaslr_const(unsigned long);
int main(void) {
    static const unsigned long values[] = {
        0, 1, 2, 0xffffffffffffffffUL, 0x5d6008cbf3848dd3UL,
        0x100000000UL, 0x8000000000000000UL
    };
    unsigned i, j;
    for (i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
        for (j = 0; j < sizeof(values)/sizeof(values[0]); ++j) {
            unsigned long ml, mh, gl, gh;
            mini_mul(values[i], values[j], &ml, &mh);
            gnu_mul(values[i], values[j], &gl, &gh);
            if (ml != gl || mh != gh) return 1;
        }
    for (i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
        if (mini_kaslr_const(values[i]) != gnu_kaslr_const(values[i])) return 2;
    return 42;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F 'mulq %r10' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux kaslr MULQ GCC differential=$result" >&2
    exit 1
}
sed 's/"=d"/"=c"/g' "$d/mini.i" >"$d/unsafe_output.i"
sed 's/"rm"/"m"/g' "$d/mini.i" >"$d/unsafe_constraint.i"
sed 's/asm("mulq %3"/asm("divq %3"/g' "$d/mini.i" >"$d/unsafe_opcode.i"
for shape in unsafe_output unsafe_constraint unsafe_opcode; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsupported x86 kaslr MULQ shape admitted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_KASLR_MUL=PASS GCC_native=49 overflow_high=1 reject=3"
