#!/usr/bin/env bash
# Original Linux checksum_64.h carry chains: preserve CF across all qwords,
# including five GNU memory inputs and the 8/16/32-byte `m` alias regions.
set -Eeuo pipefail
mini=${1:?MiniC compiler path}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/probe.i" <<'SOURCE'
unsigned long carry_reg(unsigned long sum, unsigned long other) {
    asm("addq %1,%0\n\tadcq $0,%0" : "+r"(sum) : "r"(other));
    return sum;
}
unsigned long carry_five(unsigned long sum, const unsigned long m[5]) {
    asm("addq %1,%0\n\tadcq %2,%0\n\tadcq %3,%0\n\t"
        "adcq %4,%0\n\tadcq %5,%0\n\tadcq $0,%0"
        : "+r"(sum) : "m"(m[0]), "m"(m[1]), "m"(m[2]),
                       "m"(m[3]), "m"(m[4]));
    return sum;
}
unsigned long carry_32(unsigned long sum, const void *buff) {
    asm("addq 0*8(%[src]),%[res]\n\tadcq 1*8(%[src]),%[res]\n\t"
        "adcq 2*8(%[src]),%[res]\n\tadcq 3*8(%[src]),%[res]\n\t"
        "adcq $0,%[res]" : [res] "+r"(sum)
        : [src] "r"(buff), "m"(*(const char(*)[32])buff));
    return sum;
}
unsigned long carry_16(unsigned long sum, const void *buff) {
    asm("addq 0*8(%[src]),%[res]\n\tadcq 1*8(%[src]),%[res]\n\t"
        "adcq $0,%[res]" : [res] "+r"(sum)
        : [src] "r"(buff), "m"(*(const char(*)[16])buff));
    return sum;
}
unsigned long carry_8(unsigned long sum, const void *buff) {
    asm("addq 0*8(%[src]),%[res]\n\tadcq $0,%[res]"
        : [res] "+r"(sum)
        : [src] "r"(buff), "m"(*(const char(*)[8])buff));
    return sum;
}
unsigned long carry_tail(unsigned long sum, unsigned long trail) {
    asm("addq %[trail],%[res]\n\tadcq $0,%[res]"
        : [res] "+r"(sum) : [trail] "r"(trail));
    return sum;
}
SOURCE

"$mini" --target=x86_64-linux -S "$work/probe.i" -o "$work/probe.s"
cc -c "$work/probe.s" -o "$work/probe.o"
gcc -x c -c "$work/probe.i" -o "$work/gnu-probe.o"

cat > "$work/harness.c" <<'SOURCE'
#include <stdint.h>
#include <stdio.h>
extern unsigned long carry_reg(unsigned long, unsigned long);
extern unsigned long carry_five(unsigned long, const unsigned long *);
extern unsigned long carry_32(unsigned long, const void *);
extern unsigned long carry_16(unsigned long, const void *);
extern unsigned long carry_8(unsigned long, const void *);
extern unsigned long carry_tail(unsigned long, unsigned long);
static uint64_t seed = UINT64_C(0x9e3779b97f4a7c15);
static uint64_t next_random(void) {
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}
static unsigned long add_with_carry(unsigned long a, unsigned long b) {
    const unsigned __int128 wide = (unsigned __int128)a + b;
    return (unsigned long)wide + (unsigned long)(wide >> 64);
}
int main(void) {
    unsigned long m[5];
    size_t i, j;
    for (i = 0; i < 2048; ++i) {
        const unsigned long sum = i & 1U ? ~0UL : next_random();
        unsigned long expected = sum;
        for (j = 0; j < 5; ++j) {
            m[j] = (i & 2U) ? ~0UL : next_random();
            expected = add_with_carry(expected, m[j]);
        }
        if (carry_reg(sum, m[0]) != add_with_carry(sum, m[0]) ||
            carry_tail(sum, m[0]) != add_with_carry(sum, m[0]) ||
            carry_five(sum, m) != expected ||
            carry_32(sum, m) != add_with_carry(add_with_carry(
                add_with_carry(add_with_carry(sum, m[0]), m[1]), m[2]), m[3]) ||
            carry_16(sum, m) != add_with_carry(add_with_carry(sum, m[0]), m[1]) ||
            carry_8(sum, m) != add_with_carry(sum, m[0])) {
            fprintf(stderr, "checksum CF mismatch at iteration %zu\n", i);
            return 1;
        }
    }
    puts("MINIC_X86_CSUM_CARRY_NATIVE=PASS cases=2048 forms=6");
    return 0;
}
SOURCE
gcc -std=c11 -O2 -Wall -Wextra -Werror -c "$work/harness.c" -o "$work/harness.o"
gcc -no-pie "$work/harness.o" "$work/probe.o" -o "$work/minic"
"$work/minic"
gcc -no-pie "$work/harness.o" "$work/gnu-probe.o" -o "$work/gnu"
"$work/gnu"

cat > "$work/invalid.i" <<'SOURCE'
unsigned long broken(unsigned long sum, unsigned long other) {
    asm("addq %1,%0\n\tadcq $1,%0" : "+r"(sum) : "r"(other));
    return sum;
}
SOURCE
if "$mini" --target=x86_64-linux -S "$work/invalid.i" \
    -o "$work/invalid.s" >"$work/invalid.log" 2>&1; then
    printf 'unsupported checksum carry-chain mutation was accepted\n' >&2
    exit 1
fi
test ! -e "$work/invalid.s"
echo "MINIC_X86_CSUM_CARRY=PASS exact_original_forms=6 malformed_rejected=1"
