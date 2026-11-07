#!/usr/bin/env bash
# Linux vDSO LSL -> RDPID alternative is a privileged-descriptor-sensitive
# probe. Verify *both* GNU object patchtable sections, exact feature number,
# replacement bytes and relocations; never execute an invalid LSL selector.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/mini.i" <<'SRC'
unsigned int mini_cpunode(void) {
    unsigned int p;
    asm volatile(
        "# ALT: oldnstr\n"
        "661:\n\tlsl %[seg],%[p]\n"
        "662:\n# ALT: padding\n"
        ".skip -(((6651f-6641f)-(662b-661b)) > 0) * "
        "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n .long 6641f - .\n"
        " .4byte (16*32+22)\n"
        " .byte 663b-661b\n .byte 6651f-6641f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement, \"ax\"\n"
        "# ALT: replacement 1\n"
        "6641:\n\t.byte 0xf3,0x0f,0xc7,0xf8\n"
        "6651:\n.popsection\n"
        : [p] "=a"(p) : "i"(0), [seg] "r"(15*8+3));
    return p;
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
for obj in "$d/mini.o" "$d/gnu.o"; do
    readelf -SW "$obj" | grep -Eq '\.altinstructions[[:space:]]+PROGBITS.*00000e'
    readelf -SW "$obj" | grep -Eq '\.altinstr_replacement[[:space:]]+PROGBITS.*000004'
    readelf -r "$obj" | grep -q '.rela.altinstructions'
    objdump -dr "$obj" | grep -Eq '[[:space:]]lsl[[:space:]]'
    readelf -x .altinstr_replacement "$obj" | grep -qi 'f30fc7f8'
done
sed 's/16\*32+22/16*32+23/' "$d/mini.i" > "$d/negative.i"
if "$mini" --target=x86_64-linux -S "$d/negative.i" -o "$d/negative.s" \
    >/dev/null 2>&1; then
    echo 'X86_VDSO_ALT_FAIL=changed CPUID feature admitted' >&2
    exit 1
fi
test ! -e "$d/negative.s"
echo 'MINIC_X86_VDSO_ALT_GNU_AS=PASS LSL=1 RDPID=1 alt_record=14 replacement=4 relocs=2 negative=1'
