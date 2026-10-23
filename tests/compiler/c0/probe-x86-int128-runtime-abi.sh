#!/usr/bin/env bash
# GNU/GCC ABI differential: MiniC implements the C functions under test.
# Validate 2xINTEGER register pairs, return in rdx:rax and stack fallback.
set -Eeuo pipefail
if (( $# != 1 )); then echo "usage: $0 /path/to/minic-cc" >&2; exit 64; fi
cc_minic="$1"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/ti.i" <<'SRC'
typedef unsigned __int128 U;
typedef __int128 I;
U ti_add(U a, U b) { return a+b; }
U ti_xor(U a, U b) { return a^b; }
I ti_neg(I a) { return -a; }
U ti_passthrough(U a) { return a; }
U ti_stack(unsigned long a, unsigned long b, unsigned long c,
           unsigned long d, unsigned long e, U wide) {
    return wide + a + b + c + d + e;
}
SRC
"$cc_minic" --target=x86_64-linux -S "$work/ti.i" -o "$work/ti.s"
cat >"$work/main.c" <<'SRC'
typedef unsigned __int128 U;
typedef __int128 I;
typedef unsigned long u64;
union Pair { U all; struct { u64 lo,hi; } p; };
extern U ti_add(U,U),ti_xor(U,U),ti_passthrough(U);
extern I ti_neg(I);
extern U ti_stack(u64,u64,u64,u64,u64,U);
static U pair(u64 high,u64 low) {
    union Pair p; p.p.hi=high; p.p.lo=low; return p.all;
}
static int eq(U a,u64 hi,u64 lo) {
    union Pair p; p.all=a; return p.p.hi==hi && p.p.lo==lo;
}
int main(void) {
    const U a=pair(1,~(u64)0);
    const U b=pair(0,1);
    if(!eq(ti_add(a,b),2,0)) return 1;
    if(!eq(ti_xor(a,b),1,~(u64)1)) return 2;
    if(!eq((U)ti_neg((I)pair(0,1)),~(u64)0,~(u64)0)) return 3;
    if(!eq(ti_passthrough(pair(0xabcUL,0xdefUL)),0xabcUL,0xdefUL)) return 4;
    if(!eq(ti_stack(1,2,3,4,5,pair(0x123UL,~(u64)0)),0x124UL,14UL)) return 5;
    return 0;
}
SRC
cc -O1 -fno-plt -no-pie "$work/main.c" "$work/ti.s" -o "$work/ti-test"
"$work/ti-test"
echo "X86_TI_ABI=PASS register_pair=1 return_pair=1 stack_fallback=1 cases=5"
