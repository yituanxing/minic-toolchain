#!/usr/bin/env bash
# Original Linux 6.6.143 AArch64 io.h genuine "rZ" value + "r" pointer
# register raw writes, 8/16/32/64 device loads and alternatives.
# GNU ELF + actual user-mode QEMU verifies hardware semantics and exact widths.
set -Eeuo pipefail
cc=${1:?MiniC compiler required}
gnu=aarch64-linux-gnu-gcc
qemu=qemu-aarch64
for cmd in "$gnu" "$qemu"; do command -v "$cmd" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/raw-io.i" <<'SRC'
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
void raw8(u8 val, volatile u8 *ptr) {
    asm volatile("strb %w0, [%1]" : : "rZ"(val), "r"(ptr));
}
void raw16(u16 val, volatile u16 *ptr) {
    asm volatile("strh %w0, [%1]" : : "rZ"(val), "r"(ptr));
}
void raw32(u32 val, volatile u32 *ptr) {
    asm volatile("str %w0, [%1]" : : "rZ"(val), "r"(ptr));
}
void raw64(u64 val, volatile u64 *ptr) {
    asm volatile("str %x0, [%1]" : : "rZ"(val), "r"(ptr));
}
u8 raw_read8(const volatile u8 *addr) {
    u8 val;
    asm volatile(
        ".if 1 == 1\n"
        "661:\n\t"
        "ldrb %w0, [%1]\n"
        "662:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .word 661b - .\n"
        " .word 663f - .\n"
        " .hword 91\n"
        " .byte 662b-661b\n"
        " .byte 664f-663f\n"
        ".popsection\n"
        ".subsection 1\n"
        "663:\n\t"
        "ldarb %w0, [%1]\n"
        "664:\n\t"
        ".org . - (664b-663b) + (662b-661b)\n\t"
        ".org . - (662b-661b) + (664b-663b)\n\t"
        ".previous\n"
        ".endif\n"
        : "=r"(val) : "r"(addr));
    return val;
}

u16 raw_read16(const volatile u16 *addr) {
    u16 val;
    asm volatile(
        ".if 1 == 1\n"
        "661:\n\t"
        "ldrh %w0, [%1]\n"
        "662:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .word 661b - .\n"
        " .word 663f - .\n"
        " .hword 91\n"
        " .byte 662b-661b\n"
        " .byte 664f-663f\n"
        ".popsection\n"
        ".subsection 1\n"
        "663:\n\t"
        "ldarh %w0, [%1]\n"
        "664:\n\t"
        ".org . - (664b-663b) + (662b-661b)\n\t"
        ".org . - (662b-661b) + (664b-663b)\n\t"
        ".previous\n"
        ".endif\n"
        : "=r"(val) : "r"(addr));
    return val;
}

u32 raw_read32(const volatile u32 *addr) {
    u32 val;
    asm volatile(
        ".if 1 == 1\n"
        "661:\n\t"
        "ldr %w0, [%1]\n"
        "662:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .word 661b - .\n"
        " .word 663f - .\n"
        " .hword 91\n"
        " .byte 662b-661b\n"
        " .byte 664f-663f\n"
        ".popsection\n"
        ".subsection 1\n"
        "663:\n\t"
        "ldar %w0, [%1]\n"
        "664:\n\t"
        ".org . - (664b-663b) + (662b-661b)\n\t"
        ".org . - (662b-661b) + (664b-663b)\n\t"
        ".previous\n"
        ".endif\n"
        : "=r"(val) : "r"(addr));
    return val;
}

u64 raw_read64(const volatile u64 *addr) {
    u64 val;
    asm volatile(
        ".if 1 == 1\n"
        "661:\n\t"
        "ldr %0, [%1]\n"
        "662:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .word 661b - .\n"
        " .word 663f - .\n"
        " .hword 91\n"
        " .byte 662b-661b\n"
        " .byte 664f-663f\n"
        ".popsection\n"
        ".subsection 1\n"
        "663:\n\t"
        "ldar %0, [%1]\n"
        "664:\n\t"
        ".org . - (664b-663b) + (662b-661b)\n\t"
        ".org . - (662b-661b) + (664b-663b)\n\t"
        ".previous\n"
        ".endif\n"
        : "=r"(val) : "r"(addr));
    return val;
}
void prefetch_read(const void *ptr) {
    asm volatile("prfm pldl1keep, %a0\n" : : "p"(ptr));
}
void prefetch_write(const void *ptr) {
    asm volatile("prfm pstl1keep, %a0\n" : : "p"(ptr));
}
int main(void) {
    volatile u8 b=0; volatile u16 h=0; volatile u32 w=0;
    volatile u64 d=0;
    raw8(0xa5, &b);
    raw16(0xa52c, &h);
    raw32(0x87654321U, &w);
    raw64(0x8877665544332211ULL, &d);
    prefetch_read((const void *)&b);
    prefetch_write((const void *)&d);
    if (raw_read8(&b) != b || raw_read16(&h) != h ||
        raw_read32(&w) != w || raw_read64(&d) != d)
        return 2;
    return b==0xa5 && h==0xa52c && w==0x87654321U &&
           d==0x8877665544332211ULL ? 42 : 1;
}
SRC
"$gnu" -static -O0 -x c "$work/raw-io.i" -o "$work/gnu.elf"
"$cc" --target=aarch64-linux -S "$work/raw-io.i" -o "$work/mini.s"
for ins in 'strb w9, [x10]' 'strh w9, [x10]'            'str w9, [x10]' 'str x9, [x10]' 'prfm pldl1keep, [x9]' 'prfm pstl1keep, [x9]' \
           'ldrb w9, [x10]' 'ldrh w9, [x10]' \
           'ldr w9, [x10]' 'ldr x9, [x10]' \
           'ldarb w9, [x10]' 'ldarh w9, [x10]' \
           'ldar w9, [x10]' 'ldar x9, [x10]'; do
    grep -Fq "$ins" "$work/mini.s"
done
"$gnu" -c -x assembler "$work/mini.s" -o "$work/mini.o"
aarch64-linux-gnu-readelf -r "$work/mini.o" > "$work/mini.reloc"
count=$(grep -c 'R_AARCH64_PREL32' "$work/mini.reloc" || true)
test "$count" -eq 8 || { echo "raw-read PREL32=$count != 8" >&2; exit 1; }
"$gnu" -static -O0 -x assembler "$work/mini.s" -o "$work/mini.elf"
for name in gnu mini; do
    rc=0
    "$qemu" "$work/$name.elf" || rc=$?
    test "$rc" -eq 42 || { echo "MMIO $name QEMU exit=$rc" >&2; exit 1; }
done
python3 - "$work/raw-io.i" "$work" <<'PY'
import pathlib,sys
src=pathlib.Path(sys.argv[1]).read_text()
dest=pathlib.Path(sys.argv[2])
samples={
  "extra-instruction": ('"str %w0, [%1]"', '"str %w0, [%1]; nop"'),
  "wrong-constraint": ('"r"(ptr)', '"Qo"(*ptr)'),
  "wrong-width": ('"strh %w0, [%1]"', '"str %w0, [%1]"'),
  "wrong-input": ('"rZ"(val)', '"i"(val)'),
  "no-volatile": ('asm volatile("strb', 'asm("strb'),
  "added-clobber": ('"r"(ptr));', '"r"(ptr) : "memory");'),
  "prefetch-extra": ('"prfm pldl1keep, %a0\\n"', '"prfm pldl1keep, %a0; nop\\n"'),
  "prefetch-constraint": ('"p"(ptr));', '"i"(ptr));'),
  "prefetch-nonvolatile": ('asm volatile("prfm', 'asm("prfm'),
  "read-cpucap": ('" .hword 91\\n"', '" .hword 92\\n"'),
  "read-new-insn": ('"ldarb %w0, [%1]\\n"', '"ldarh %w0, [%1]\\n"'),
  "read-old-insn": ('"ldrb %w0, [%1]\\n"', '"ldrb %w0, [%1]; nop\\n"'),
  "read-output": (': "=r"(val)', ': "=m"(val)'),
  "read-input": (': "=r"(val) : "r"(addr)', ': "=r"(val) : "i"(addr)')
}
for name,(old,new) in samples.items():
    if old not in src: raise SystemExit("bad original GNU mutation "+name)
    (dest/("bad-"+name+".i")).write_text(src.replace(old,new,1))
PY
for bad in extra-instruction wrong-constraint wrong-width wrong-input            no-volatile added-clobber prefetch-extra prefetch-constraint prefetch-nonvolatile read-cpucap read-new-insn read-old-insn read-output read-input; do
    if "$cc" --target=aarch64-linux -S "$work/bad-$bad.i"         -o "$work/bad-$bad.s" >"$work/$bad.out" 2>"$work/$bad.err"; then
        echo "FAIL raw MMIO unsafe mutation accepted: $bad" >&2
        exit 1
    fi
    test ! -e "$work/bad-$bad.s"
done
echo 'MINIC_AARCH64_LINUX_RAW_MMIO_GNU_QEMU=PASS store_widths=4 read_alt_widths=4 prefetch_hints=2 negative=14 cpucap=91 PREL32=8 target_hardware=1'
