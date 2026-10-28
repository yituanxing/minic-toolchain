#!/usr/bin/env bash
# Original Linux 6.6.143 AArch64 io.h raw 8/16/32/64 device stores.
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
    asm volatile("strb %w0, %1" : : "rZ"(val), "Qo"(*ptr));
}
void raw16(u16 val, volatile u16 *ptr) {
    asm volatile("strh %w0, %1" : : "rZ"(val), "Qo"(*ptr));
}
void raw32(u32 val, volatile u32 *ptr) {
    asm volatile("str %w0, %1" : : "rZ"(val), "Qo"(*ptr));
}
void raw64(u64 val, volatile u64 *ptr) {
    asm volatile("str %x0, %1" : : "rZ"(val), "Qo"(*ptr));
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
    return b==0xa5 && h==0xa52c && w==0x87654321U &&
           d==0x8877665544332211ULL ? 42 : 1;
}
SRC
"$gnu" -static -O0 -x c "$work/raw-io.i" -o "$work/gnu.elf"
"$cc" --target=aarch64-linux -S "$work/raw-io.i" -o "$work/mini.s"
for ins in 'strb w9, [x10]' 'strh w9, [x10]'            'str w9, [x10]' 'str x9, [x10]' 'prfm pldl1keep, [x9]' 'prfm pstl1keep, [x9]'; do
    grep -Fq "$ins" "$work/mini.s"
done
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
  "extra-instruction": ('"str %w0, %1"', '"str %w0, %1; nop"'),
  "wrong-constraint": ('"Qo"(*ptr)', '"r"(*ptr)'),
  "wrong-width": ('"strh %w0, %1"', '"str %w0, %1"'),
  "wrong-input": ('"rZ"(val)', '"i"(val)'),
  "no-volatile": ('asm volatile("strb', 'asm("strb'),
  "added-clobber": ('"Qo"(*ptr));', '"Qo"(*ptr) : "memory");'),
  "prefetch-extra": ('"prfm pldl1keep, %a0\\n"', '"prfm pldl1keep, %a0; nop\\n"'),
  "prefetch-constraint": ('"p"(ptr));', '"i"(ptr));'),
  "prefetch-nonvolatile": ('asm volatile("prfm', 'asm("prfm')
}
for name,(old,new) in samples.items():
    if old not in src: raise SystemExit("bad original GNU mutation "+name)
    (dest/("bad-"+name+".i")).write_text(src.replace(old,new,1))
PY
for bad in extra-instruction wrong-constraint wrong-width wrong-input            no-volatile added-clobber prefetch-extra prefetch-constraint prefetch-nonvolatile; do
    if "$cc" --target=aarch64-linux -S "$work/bad-$bad.i"         -o "$work/bad-$bad.s" >"$work/$bad.out" 2>"$work/$bad.err"; then
        echo "FAIL raw MMIO unsafe mutation accepted: $bad" >&2
        exit 1
    fi
    test ! -e "$work/bad-$bad.s"
done
echo 'MINIC_AARCH64_LINUX_RAW_MMIO_GNU_QEMU=PASS store_widths=4 prefetch_hints=2 negative=9 target_hardware=1'
