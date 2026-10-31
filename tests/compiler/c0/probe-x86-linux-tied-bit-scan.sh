#!/usr/bin/env bash
# Actual GNU Linux 6.6.143 bitops.h: fls/variable_ffs/fls64 with
# 32-bit tied '"0"(-1)' / 64-bit '+r' initial destination.
set -Eeuo pipefail
minic=$1; work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/bits.i" <<'EOF'
int mini_bsf32(int x) {
    int r;
    asm("bsfl %1,%0" : "=r"(r) : "rm"(x), "0"(-1));
    return r + 1;
}
int mini_bsr32(unsigned int x) {
    int r;
    asm("bsrl %1,%0" : "=r"(r) : "rm"(x), "0"(-1));
    return r + 1;
}
int mini_bsr64(unsigned long x) {
    int bitpos = -1;
    asm("bsrq %1,%q0" : "+r"(bitpos) : "rm"(x));
    return bitpos + 1;
}
EOF
sed 's/mini_/gcc_/g' "$work/bits.i" > "$work/gcc.c"
cat > "$work/main.c" <<'EOF'
extern int mini_bsf32(int),mini_bsr32(unsigned),mini_bsr64(unsigned long);
extern int gcc_bsf32(int),gcc_bsr32(unsigned),gcc_bsr64(unsigned long);
int main(void){
    static const unsigned long v[] = {1UL,2UL,3UL,4UL,7UL,8UL,
        0xffUL,0x80000000UL,0xffffffffUL,0x100000000UL,~0UL};
    unsigned long i;
    for(i=0;i<sizeof(v)/sizeof(v[0]);++i) {
        unsigned n=(unsigned)v[i];
        if(n && (mini_bsf32((int)n)!=gcc_bsf32((int)n) ||
                 mini_bsr32(n)!=gcc_bsr32(n))) return 1;
        if(mini_bsr64(v[i])!=gcc_bsr64(v[i]))return 2;
    }
    return 42;
}
EOF
"$minic" --target=x86_64-linux -S "$work/bits.i" -o "$work/bits.s"
for op in 'bsfl %r10d, %eax' 'bsrl %r10d, %eax' 'bsrq %r10, %rax'; do
    grep -Fq "$op" "$work/bits.s"
done
cc -O0 -no-pie "$work/main.c" "$work/gcc.c" -x assembler "$work/bits.s" \
    -o "$work/oracle"
set +e
"$work/oracle"; code=$?
set -e
test "$code" -eq 42 || { echo "FAIL x86 BSF/BSR GNU differential rc=$code"; exit 1; }
cat > "$work/injected.i" <<'EOF'
int bad(int x) {
    int r;
    asm("bsrl %1,%0; cli" : "=r"(r) : "rm"(x), "0"(-1));
    return r;
}
EOF
if "$minic" --target=x86_64-linux -S "$work/injected.i" \
    -o "$work/injected.s" >/dev/null 2>&1; then
    echo 'FAIL injected BSF/BSR suffix admitted'; exit 1
fi
cat > "$work/wrong.i" <<'EOF'
int bad(int x) {
    int r;
    asm("bsrl %1,%0" : "=r"(r) : "rm"(x), "r"(-1));
    return r;
}
EOF
if "$minic" --target=x86_64-linux -S "$work/wrong.i" \
    -o "$work/wrong.s" >/dev/null 2>&1; then
    echo 'FAIL untied BSR seed admitted'; exit 1
fi
echo 'MINIC_X86_LINUX_BSF_BSR_GNU_NATIVE=PASS family=3 cases=33 injected_rejected=1 untied_rejected=1'
