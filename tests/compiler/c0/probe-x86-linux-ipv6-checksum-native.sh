#!/usr/bin/env bash
# Five dependent x86 ADD/ADC instructions from Linux IPv6 pseudo-header sum.
# Comparison uses GCC's original inline ASM, including end-around carry.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/mini.i" <<'SRC'
typedef unsigned long long u64;
u64 mini_ipv6(const u64 *saddr, const u64 *daddr, u64 rest) {
    u64 sum64;
    asm("addq (%[saddr]),%[sum]\n\t"
        "adcq 8(%[saddr]),%[sum]\n\t"
        "adcq (%[daddr]),%[sum]\n\t"
        "adcq 8(%[daddr]),%[sum]\n\t"
        "adcq $0,%[sum]\n"
        : [sum] "=r"(sum64)
        : "[sum]"(rest), [saddr] "r"(saddr), [daddr] "r"(daddr));
    return sum64;
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
cat > "$d/main.c" <<'SRC'
#include <stdint.h>
extern uint64_t mini_ipv6(const uint64_t*,const uint64_t*,uint64_t);
extern uint64_t gnu_ipv6(const uint64_t*,const uint64_t*,uint64_t);
int main(void) {
    for (uint64_t n=0;n<512;n++) {
        uint64_t s[2]={~0ULL-(n&31),n*0x9e3779b97f4a7c15ULL};
        uint64_t d[2]={n*0x1001001001001ULL,~0ULL-((n*7)&255)};
        uint64_t init=n&1 ? ~0ULL : n*0xc3e734abcdUL;
        if(mini_ipv6(s,d,init)!=gnu_ipv6(s,d,init)) return 1;
    }
    return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
sed 's/adcq \$0,%\[sum\]/subq $0,%[sum]/' "$d/mini.i" > "$d/negative.i"
if "$mini" --target=x86_64-linux -S "$d/negative.i" -o "$d/negative.s" \
    >/dev/null 2>&1; then
    echo 'X86_IPV6_FAIL=unadmitted carry-chain mutation' >&2
    exit 1
fi
test ! -e "$d/negative.s"
echo 'MINIC_X86_IPV6_NATIVE=PASS carry_chain=512 negative=1'
