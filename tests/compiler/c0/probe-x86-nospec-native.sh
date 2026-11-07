#!/usr/bin/env bash
# Original Linux x86 array_index_mask_nospec hardware CF/SBB GNU differential.
set -Eeuo pipefail
minic=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/mask.i" <<'EOF'
unsigned long mini64(unsigned long index,unsigned long size) {
    unsigned long mask;
    asm volatile("cmp %1,%2; sbb %0,%0;" : "=r"(mask) : "g"(size), "r"(index) : "cc");
    return mask;
}
unsigned long mini32(unsigned int index,unsigned int size) {
    unsigned long mask;
    asm volatile("cmp %1,%2; sbb %0,%0" : "=r"(mask) : "g"(size), "r"(index) : "cc");
    return mask;
}
EOF
sed 's/mini64/gcc64/;s/mini32/gcc32/' "$work/mask.i" >"$work/gcc.c"
cat >"$work/main.c" <<'EOF'
extern unsigned long mini64(unsigned long,unsigned long),gcc64(unsigned long,unsigned long);
extern unsigned long mini32(unsigned int,unsigned int),gcc32(unsigned int,unsigned int);
int main(void) {
    static const unsigned long cases[] = {0UL,1UL,2UL,42UL,255UL,65536UL,
        0xffffffffUL,0x100000000UL,~0UL};
    unsigned long i,j;
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i)
        for(j=0;j<sizeof(cases)/sizeof(cases[0]);++j){
            unsigned long a=cases[i],b=cases[j];
            if(mini64(a,b)!=gcc64(a,b) ||
               mini32((unsigned int)a,(unsigned int)b)!=
               gcc32((unsigned int)a,(unsigned int)b))return 1;
        }
    return 42;
}
EOF
"$minic" --target=x86_64-linux -S "$work/mask.i" -o "$work/mask.s"
grep -Fq '  cmpq %r10, %r11' "$work/mask.s"
grep -Fq '  cmpl %r10d, %r11d' "$work/mask.s"
grep -Fq '  sbbq %r8, %r8' "$work/mask.s"
cc -O0 -no-pie "$work/main.c" "$work/gcc.c" -x assembler "$work/mask.s" -o "$work/verify"
set +e
"$work/verify"
rc=$?
set -e
test "$rc" -eq 42 || { echo "FAIL nospec GCC hardware differential rc=$rc" >&2; exit 1; }
cat >"$work/unsafe.i" <<'EOF'
unsigned long bad(unsigned long idx,unsigned long sz){
    unsigned long r;
    asm volatile("cmp %1,%2; sbb %0,%0; cli" : "=r"(r) : "g"(sz), "r"(idx) : "cc");
    return r;
}
EOF
if "$minic" --target=x86_64-linux -S "$work/unsafe.i" -o "$work/unsafe.s" >"$work/unsafe.log" 2>&1; then
  echo 'FAIL injected privileged instruction admitted' >&2; exit 1
fi
cat >"$work/width.i" <<'EOF'
unsigned long bad(unsigned short idx,unsigned short sz){
    unsigned long r;
    asm volatile("cmp %1,%2; sbb %0,%0" : "=r"(r) : "g"(sz), "r"(idx) : "cc");
    return r;
}
EOF
if "$minic" --target=x86_64-linux -S "$work/width.i" -o "$work/width.s" >"$work/width.log" 2>&1; then
  echo 'FAIL unsupported 16-bit x86 mask admitted' >&2; exit 1
fi
echo 'X86_LINUX_NOSPEC_GNU_NATIVE=PASS u64_pairs=81 u32_pairs=81 injected_rejected=1 width_rejected=1'
