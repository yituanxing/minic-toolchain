#!/usr/bin/env bash
# Real Linux 6.6.143 arch/x86/kernel/signal_32.c: an unsigned-int
# selector snapshot via bare mov %%gs,%0 / %%fs,%0 with memory ordering.
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/mini.i" <<'EOF'
unsigned int mini_gs(void) {
    unsigned int v;
    asm("mov %%gs,%0" : "=r"(v) : : "memory");
    return v;
}
unsigned int mini_fs(void) {
    unsigned int v;
    asm("mov %%fs,%0" : "=r"(v) : : "memory");
    return v;
}
unsigned int mini_ds(void) {
    unsigned int v;
    asm("mov %%ds,%0" : "=r"(v) : : "memory");
    return v;
}
unsigned int mini_es(void) {
    unsigned int v;
    asm("mov %%es,%0" : "=r"(v) : : "memory");
    return v;
}
EOF
sed 's/mini_/gnu_/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
extern unsigned int mini_gs(void), mini_fs(void), mini_ds(void), mini_es(void);
extern unsigned int gnu_gs(void), gnu_fs(void), gnu_ds(void), gnu_es(void);
int main(void) {
    return mini_gs() == gnu_gs() && mini_fs() == gnu_fs() &&
           mini_ds() == gnu_ds() && mini_es() == gnu_es() &&
           (mini_gs() & ~0xffffU) == 0 ? 42 : 1;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F 'movl %gs, %eax' "$d/mini.s" >/dev/null
grep -F 'movl %fs, %eax' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux u32 segment selector GCC differential=$result" >&2
    exit 1
}
sed 's/unsigned int v;/unsigned long v;/g' "$d/mini.i" >"$d/unsafe_width.i"
sed 's/"=r"/"=m"/g' "$d/mini.i" >"$d/unsafe_constraint.i"
sed 's/ : : "memory"/ : : "rax"/g' "$d/mini.i" >"$d/unsafe_clobber.i"
for shape in unsafe_width unsafe_constraint unsafe_clobber; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe x86 segment selector read admitted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_SEGMENT_U32_READ=PASS GCC_native=4 no_upper_garbage=1 reject=3"
