#!/usr/bin/env bash
# Linux 6.6.143 include/linux/math64.h mul_u64_u64_div_u64(): original
# unsigned 128-bit MULQ intermediate followed directly by DIVQ, RDX clobber.
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/mini.i" <<'EOF'
typedef unsigned long long u64;
u64 mini_muldiv(u64 a, u64 mul, u64 div) {
    u64 q;
    asm("mulq %2; divq %3" : "=a"(q)
        : "a"(a), "rm"(mul), "rm"(div) : "rdx");
    return q;
}
EOF
sed 's/mini_muldiv/gnu_muldiv/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
typedef unsigned long long u64;
extern u64 mini_muldiv(u64,u64,u64), gnu_muldiv(u64,u64,u64);
int main(void) {
    static const u64 a[] = {0,1,2,0xffffffffULL,
                            0x8000000000000000ULL,0xffffffffffffffffULL};
    static const u64 mul[] = {0,1,2,0x100000000ULL,
                              0x5d6008cbf3848dd3ULL,0xffffffffffffffffULL};
    unsigned i,j;
    for (i = 0; i < sizeof(a)/sizeof(a[0]); ++i)
        for (j = 0; j < sizeof(mul)/sizeof(mul[0]); ++j) {
            /* Divisor >= multiplier mathematically bounds quotient <= a;
             * no #DE is possible in these real CPU execution probes. */
            u64 div = mul[j] ? mul[j] : 1;
            u64 m = mini_muldiv(a[i],mul[j],div);
            u64 g = gnu_muldiv(a[i],mul[j],div);
            if (m != g) return 1;
            if (div == mul[j] && m != a[i]) return 2;
            if (mini_muldiv(a[i],mul[j],~0ULL) !=
                gnu_muldiv(a[i],mul[j],~0ULL)) return 3;
        }
    return 42;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F 'mulq %r10' "$d/mini.s" >/dev/null
grep -F 'divq %r11' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux unsigned 128/64 MULQ DIVQ GNU differential=$result" >&2
    exit 1
}
sed 's/"rdx"/"rcx"/g' "$d/mini.i" >"$d/unsafe_clobber.i"
sed 's/"rm"(div)/"r"(div)/g' "$d/mini.i" >"$d/unsafe_divisor.i"
sed 's/"mulq %2; divq %3"/"mulq %2; idivq %3"/g' "$d/mini.i" >"$d/unsafe_signed.i"
sed 's/u64 q;/unsigned int q;/' "$d/mini.i" >"$d/unsafe_width.i"
for shape in unsafe_clobber unsafe_divisor unsafe_signed unsafe_width; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe MULQ DIVQ GNU contract admitted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_MULDIV_U64=PASS GNU_native=72 div0_hardware=untouched reject=4"
