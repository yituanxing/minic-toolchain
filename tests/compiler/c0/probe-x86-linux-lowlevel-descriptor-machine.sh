#!/usr/bin/env bash
# Source-qualified Linux LLDT/INVPCID -> physical x86 MachineIR -> GNU AS.
# These are privileged instructions: assemble only, never execute them.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/mini.i" <<'SRC'
void mini_lldt(unsigned int value) {
    asm volatile("lldt %w0" :: "q"(value));
}
void mini_lldt_word(unsigned short value) {
    asm volatile("lldt %w0" :: "q"(value));
}
void mini_lldt_long(unsigned long value) {
    asm volatile("lldt %w0" :: "q"(value));
}
void mini_sldt(unsigned short *p) {
    asm("sldt %0" : "=m"(*p));
}
void mini_fs(unsigned int value) {
    asm volatile("mov %0, %%fs" :: "rm"(value));
}
struct Desc { unsigned long d[2]; };
void mini_invpcid(struct Desc *p, unsigned long type) {
    asm volatile("invpcid %[desc], %[type]"
                 :: [desc] "m"(*p), [type] "r"(type) : "memory");
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -c -O0 "$d/gnu.c" -o "$d/gnu.o"
objdump -dr "$d/mini.o" > "$d/mini.dump"
objdump -dr "$d/gnu.o" > "$d/gnu.dump"
test "$(grep -Ec '[[:space:]]lldt[[:space:]]' "$d/mini.dump")" -eq 3
test "$(grep -Ec '[[:space:]]lldt[[:space:]]' "$d/gnu.dump")" -eq 3
grep -q 'invpcid' "$d/mini.dump"
grep -q 'invpcid' "$d/gnu.dump"
grep -q 'sldt' "$d/mini.dump"
grep -q 'sldt' "$d/gnu.dump"
grep -q '%fs' "$d/mini.dump"
grep -q '%fs' "$d/gnu.dump"
grep -q 'lldt %r10w' "$d/mini.s"
grep -q 'invpcid (%r10), %r11' "$d/mini.s"
grep -q 'sldt (%r10)' "$d/mini.s"
grep -q 'movw %r10w, %fs' "$d/mini.s"

# Negative source and target physical contracts: no arbitrary passthrough,
# malformed constraints, wrong architectural width, or bad descriptor size.
sed 's/"q"(value)/"r"(value)/g' "$d/mini.i" > "$d/bad-constraint.i"
sed 's/"invpcid %\[desc\], %\[type\]"/"invpcid %[desc], %[type]; cli"/' \
    "$d/mini.i" > "$d/bad-template.i"
sed 's/unsigned long d\[2\]/unsigned long d[1]/' \
    "$d/mini.i" > "$d/bad-descriptor.i"
sed 's/struct Desc \*p, unsigned long type/struct Desc *p, unsigned int type/' \
    "$d/mini.i" > "$d/bad-type.i"
sed 's/"=m"(\*p)/"=r"(*p)/' "$d/mini.i" > "$d/bad-sldt-constraint.i"
sed 's/unsigned short \*p/unsigned long *p/' \
    "$d/mini.i" > "$d/bad-sldt-width.i"
sed 's/"rm"(value)/"r"(value)/' "$d/mini.i" > "$d/bad-fs-constraint.i"
sed 's/mov %0, %%fs/mov %0, %%cs/' "$d/mini.i" > "$d/bad-fs-template.i"
for case in constraint template descriptor type sldt-constraint sldt-width \
    fs-constraint fs-template; do
    if "$mini" --target=x86_64-linux -S "$d/bad-$case.i" \
        -o "$d/bad-$case.s" >/dev/null 2>&1; then
        echo "X86_LOWLEVEL_DESCRIPTOR_FAIL=accepted_bad_$case" >&2
        exit 1
    fi
    test ! -e "$d/bad-$case.s"
done
echo 'MINIC_X86_LOWLEVEL_DESCRIPTOR=PASS GNU_AS=6 negative=8'
