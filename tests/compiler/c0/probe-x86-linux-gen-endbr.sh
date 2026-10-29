#!/usr/bin/env bash
# Linux 6.6.143 arch/x86/include/asm/ibt.h gen_endbr exact expanded GNU asm:
# a nonvolatile early-clobber 32-bit output receives the IBT opcode value.
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/mini.i" <<'EOF'
typedef unsigned int u32;
u32 mini_endbr(void) {
    u32 endbr;
    asm("mov $~0xfa1e0ff3, %[endbr]\n\t"
        "not %[endbr]\n\t"
        : [endbr] "=&r" (endbr));
    return endbr;
}
EOF
sed 's/mini_endbr/gnu_endbr/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
extern unsigned int mini_endbr(void), gnu_endbr(void);
int main(void) {
    unsigned int m = mini_endbr();
    unsigned int g = gnu_endbr();
    return m == g && m == 0xfa1e0ff3U ? 42 : 1;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F 'movl $0x05e1f00c, %eax' "$d/mini.s" >/dev/null
grep -F 'notl %eax' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux gen_endbr: GNU native differential=$result" >&2
    exit 1
}

sed 's/"=\&r"/"=r"/g' "$d/mini.i" >"$d/unsafe_constraint.i"
sed 's/u32 endbr;/unsigned long endbr;/g' "$d/mini.i" >"$d/unsafe_type.i"
sed 's/"=\&r"/"=m"/g' "$d/mini.i" >"$d/unsafe_memory_output.i"
sed 's/not %\[endbr\]/neg %[endbr]/g' "$d/mini.i" >"$d/unsafe_opcode.i"
for test_case in unsafe_constraint unsafe_type unsafe_memory_output unsafe_opcode; do
    if "$minic" --target=x86_64-linux -S "$d/$test_case.i" \
        -o "$d/$test_case.s" >"$d/$test_case.out" 2>"$d/$test_case.err"; then
        echo "FAIL unsafe x86 Linux gen_endbr variant accepted: $test_case" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_GEN_ENDBR=PASS GNU_native=1 exact_asm=1 reject=4"
