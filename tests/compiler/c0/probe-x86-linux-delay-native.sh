#!/usr/bin/env bash
# Original GNU x86 delay calibration grammar.  Multiplication and the counted
# busywait compare live C results with GCC; TPAUSE is instruction-set gated,
# so both compilers' TPAUSE encodings are assembled but never executed.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/mini.i" <<'SRC'
unsigned long mini_mul(unsigned long xloops, unsigned long lpj) {
    int d0;
    asm("mull %%edx" : "=d"(xloops), "=&a"(d0)
        : "1"(xloops), "0"(lpj));
    return xloops;
}
unsigned long mini_loop(unsigned long loops) {
    asm volatile("test %0,%0\n\tjz 3f\n\tjmp 1f\n\t"
                 ".align 16\n1:\tjmp 2f\n\t.align 16\n"
                 "2:\tdec %0\n\tjnz 2b\n3:\tdec %0\n"
        : "+a"(loops));
    return loops;
}
void mini_tpause(unsigned int ecx, unsigned int edx, unsigned int eax) {
    asm volatile("tpause %%ecx\n" :: "c"(ecx), "d"(edx), "a"(eax));
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
objdump -dr "$d/mini.o" | grep -Eq '[[:space:]](mul|tpause|dec)[[:space:]]'
cat > "$d/main.c" <<'SRC'
extern unsigned long mini_mul(unsigned long,unsigned long);
extern unsigned long gnu_mul(unsigned long,unsigned long);
extern unsigned long mini_loop(unsigned long);
extern unsigned long gnu_loop(unsigned long);
int main(void) {
    for (unsigned long i=0;i<256;i++) {
        unsigned long a=(i*0x9e3779b9UL) ^ (i<<34);
        unsigned long b=(i*0xc3e734abUL) ^ (i<<39);
        if(mini_mul(a,b)!=gnu_mul(a,b)) return 1;
    }
    for (unsigned long i=0;i<32;i++)
        if(mini_loop(i)!=gnu_loop(i)) return 2;
    return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
sed 's/mull %%edx/idivl %%edx/' "$d/mini.i" > "$d/negative.i"
if "$mini" --target=x86_64-linux -S "$d/negative.i" -o "$d/negative.s" \
    >/dev/null 2>&1; then
    echo 'X86_DELAY_FAIL=unadmitted multiply variant' >&2
    exit 1
fi
test ! -e "$d/negative.s"
echo 'MINIC_X86_DELAY_NATIVE=PASS MUL=256 LOOP=32 TPAUSE_GNU_AS=1 negative=1'
