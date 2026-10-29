#!/usr/bin/env bash
# Linux 6.6.143 arch/x86/include/asm/kexec.h crash_setup_regs(): GNU
# literal EAX output gets 32-bit zero-extended segment selector, stored u64.
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/mini.i" <<'EOF'
struct regs { unsigned long ss, cs, ds, es, fs, gs; };
void mini_segments(struct regs *r) {
    asm volatile("movl %%ss, %%eax;" : "=a"(r->ss));
    asm volatile("movl %%cs, %%eax;" : "=a"(r->cs));
    asm volatile("movl %%ds, %%eax;" : "=a"(r->ds));
    asm volatile("movl %%es, %%eax;" : "=a"(r->es));
    asm volatile("movl %%fs, %%eax;" : "=a"(r->fs));
    asm volatile("movl %%gs, %%eax;" : "=a"(r->gs));
}
EOF
sed 's/mini_segments/gnu_segments/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
struct regs { unsigned long ss, cs, ds, es, fs, gs; };
extern void mini_segments(struct regs *), gnu_segments(struct regs *);
int main(void) {
    struct regs a = {~0UL,~0UL,~0UL,~0UL,~0UL,~0UL};
    struct regs b = a;
    mini_segments(&a);
    gnu_segments(&b);
    return a.ss == b.ss && a.cs == b.cs && a.ds == b.ds &&
           a.es == b.es && a.fs == b.fs && a.gs == b.gs &&
           !(a.ss & ~0xffffUL) && !(a.cs & ~0xffffUL) ? 42 : 1;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F 'movl %ss, %eax' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux kexec EAX segment selector GNU differential=$result" >&2
    exit 1
}
sed 's/"=a"/"=r"/g' "$d/mini.i" >"$d/unsafe_constraint.i"
sed 's/unsigned long ss/unsigned int ss/' "$d/mini.i" >"$d/unsafe_width.i"
sed 's/movl %%ss, %%eax;/movq %%ss, %%rax;/' "$d/mini.i" >"$d/unsafe_opcode.i"
for shape in unsafe_constraint unsafe_width unsafe_opcode; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe x86 kexec selector snapshot admitted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_KEXEC_SEGMENTS=PASS GNU_native=6 high_bits_zero=1 reject=3"
