#!/bin/sh
# Two genuine original Linux 6.6.143 GNU asm BUG metadata variants.
# Exact target assembler, relocation, GNU executable and QEMU acceptance.
set -eu
: "${MINIC_CC:?missing exact compiler}"
: "${MINIC_LINUX_BUG_TARGET:?missing target}"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT HUP INT TERM
case "$MINIC_LINUX_BUG_TARGET" in
  aarch64)
    cc=aarch64-linux-gnu-gcc
    qemu=qemu-aarch64
    cat >"$d/source.i" <<'SRC'
void original_linux_bug(void) {
    asm volatile(".pushsection __bug_table,\"aw\"; .align 2;"
                 " 14470: .long 14471f - .;"
                 " .pushsection .rodata.str,\"aMS\",@progbits,1;"
                 " 14472: .string \"include/linux/jump_label.h\";"
                 " .popsection; .long 14472b - .;"
                 " .short 286; .short (1 << 0)|((1 << 3) | ((9) << 8));"
                 " .align 2; .popsection; 14471: brk 0x800");
}
int main(void) { return 42; }
SRC
    ;;
  loongarch64)
    cc=loongarch64-linux-gnu-gcc-13
    qemu=qemu-loongarch64
    cat >"$d/source.i" <<'SRC'
void original_linux_bug(void) {
    asm volatile(".pushsection __bug_table, \"aw\"; .align 2;"
                 " 10000: .long 10001f - .;"
                 " .pushsection .rodata.str, \"aMS\", @progbits, 1;"
                 " 10002: .string \"include/linux/thread_info.h\";"
                 " .popsection; .long 10002b - .;"
                 " .short 249; .short (1 << 0)|((1 << 1) | ((9) << 8));"
                 " .popsection; 10001: break 1");
}
int main(void) { return 42; }
SRC
    ;;
  *) echo "MINIC_LINUX_BUG=FAIL unknown ISA" >&2; exit 2 ;;
esac
"$cc" -static -x c "$d/source.i" -o "$d/original.gnu"
"$MINIC_CC" --target="$MINIC_LINUX_BUG_TARGET-linux" -S "$d/source.i" -o "$d/original.mini.s"
"$cc" -c -x assembler "$d/original.mini.s" -o "$d/original.mini.o"
"$cc" -static -x assembler "$d/original.mini.s" -o "$d/original.mini"
"$cc" -c -x c "$d/source.i" -o "$d/original.gnu.o"
readelf -SW "$d/original.mini.o" | grep -F __bug_table >/dev/null
readelf -SW "$d/original.gnu.o" | grep -F __bug_table >/dev/null
readelf -r "$d/original.mini.o" | grep -F __bug_table >/dev/null
readelf -r "$d/original.gnu.o" | grep -F __bug_table >/dev/null
set +e
timeout 45s "$qemu" "$d/original.gnu" >"$d/gnu.out" 2>"$d/gnu.err"
gnu_code=$?
timeout 45s "$qemu" "$d/original.mini" >"$d/mini.out" 2>"$d/mini.err"
mini_code=$?
set -e
if [ "$gnu_code" -ne 42 ] || [ "$mini_code" -ne 42 ] ||
   ! cmp -s "$d/gnu.out" "$d/mini.out" ||
   ! cmp -s "$d/gnu.err" "$d/mini.err"; then
    echo "MINIC_LINUX_BUG=FAIL target=$MINIC_LINUX_BUG_TARGET GNU_QEMU" >&2; exit 1
fi
sed -e 's/\.short 249/\.short .inst 0/' -e 's/\.short 286/\.short .inst 0/' \
    "$d/source.i" >"$d/bad.i"
if "$MINIC_CC" --target="$MINIC_LINUX_BUG_TARGET-linux" -S "$d/bad.i" -o "$d/bad.s" 2>"$d/bad.err"; then
    echo "MINIC_LINUX_BUG=FAIL unsafe_source_accepted" >&2; exit 1
fi
test ! -e "$d/bad.s"
echo "MINIC_LINUX_BUG=PASS target=$MINIC_LINUX_BUG_TARGET GNU_ELF_LINK_QEMU=1 unknown_asm_rejected=1"
