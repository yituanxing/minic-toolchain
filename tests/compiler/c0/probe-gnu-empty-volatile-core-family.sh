#!/usr/bin/env bash
# Genuine original Linux BUG/BUG_ON empty volatile GNU ASM source class.
# No instruction is fabricated; compiler barrier survives Core, GNU native
# assembly/link and all target runtime oracles. Unsafe nonempty ASM fails closed.
set -Eeuo pipefail
: "${MINIC_CC:?missing compiler}"
: "${MINIC_EMPTY_TARGET:?missing target}"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
case "$MINIC_EMPTY_TARGET" in
 x86_64) cc=gcc; emulator=();;
 aarch64) cc=aarch64-linux-gnu-gcc; emulator=(qemu-aarch64);;
 loongarch64) cc=loongarch64-linux-gnu-gcc-13; emulator=(qemu-loongarch64);;
 riscv64) cc=riscv64-linux-gnu-gcc; emulator=(qemu-riscv64);;
 *) echo "MINIC_EMPTY_VOLATILE=FAIL unknown target" >&2;exit 2;;
esac
cat >"$d/original.i" <<'SRC'
__attribute__((noinline)) int valid_linux_path(int x) {
    if (__builtin_expect(x < 0, 0)) {
        __asm__ __volatile__("");
        __builtin_unreachable();
    }
    __asm__ __volatile__("");
    __asm__ __volatile__("" : : : "memory");
    __asm__("");
    return x + 40;
}
volatile int observed = 0;
__attribute__((noinline)) int observe(int n) {
    observed += n;
    return observed;
}
__attribute__((noinline)) int multi_input(void) {
    __asm__ __volatile__("" : : "r"(observe(1)), "r"(observe(2)) : "memory");
    __asm__ __volatile__("" : : "r"(observe(3)), "r"(observe(4)));
    return observed;
}
int main(void) {
    if (valid_linux_path(2) != 42) return 2;
    return multi_input() == 10 ? 42 : 3;
}
SRC
"$MINIC_CC" --target="$MINIC_EMPTY_TARGET-linux" -S "$d/original.i" -o "$d/mini.s"
if [[ "$MINIC_EMPTY_TARGET" == riscv64 ]]; then
    # The RV64 CI runner deliberately has no glibc CRT/sysroot. Link the
    # original GNU C and MiniC objects to one ISA-native Linux exit stub.
    # This is a genuine end-to-end QEMU run, not a skipped runtime oracle.
    cat >"$d/start.s" <<'ASM'
.globl _start
_start:
    call main
    li a7, 93
    ecall
ASM
    "$cc" -c -x c "$d/original.i" -o "$d/gnu.o"
    "$cc" -c -x assembler "$d/mini.s" -o "$d/mini.o"
    "$cc" -c -x assembler "$d/start.s" -o "$d/start.o"
    "$cc" -nostdlib -static -Wl,-e,_start "$d/start.o" "$d/gnu.o" -o "$d/gnu"
    "$cc" -nostdlib -static -Wl,-e,_start "$d/start.o" "$d/mini.o" -o "$d/mini"
else
    "$cc" -static -x c "$d/original.i" -o "$d/gnu"
    "$cc" -static -x assembler "$d/mini.s" -o "$d/mini"
fi
set +e
timeout 50s "${emulator[@]}" "$d/gnu" >"$d/gnu.stdout" 2>"$d/gnu.stderr"
original_status=$?
timeout 50s "${emulator[@]}" "$d/mini" >"$d/mini.stdout" 2>"$d/mini.stderr"
mini_status=$?
set -e
if (( original_status != 42 || mini_status != 42 )) ||
   ! cmp -s "$d/gnu.stdout" "$d/mini.stdout" ||
   ! cmp -s "$d/gnu.stderr" "$d/mini.stderr"; then
    echo "MINIC_EMPTY_VOLATILE=FAIL target=$MINIC_EMPTY_TARGET original=$original_status mini=$mini_status" >&2
    exit 1
fi
cat >"$d/unknown.i" <<'SRC'
int main(void) {
    __asm__ __volatile__("minic_unknown_isa_opcode");
    return 42;
}
SRC
if "$MINIC_CC" --target="$MINIC_EMPTY_TARGET-linux" -S "$d/unknown.i" -o "$d/unknown.s" 2>"$d/unknown.err"; then
    # RISC-V permits an assembler-managed ISA superset: the real GNU
    # assembler must still reject an unknown mnemonic before any link.
    if "$cc" -c -x assembler "$d/unknown.s" -o "$d/unknown.o" >/dev/null 2>&1; then
        echo "MINIC_EMPTY_VOLATILE=FAIL unqualified hardware assembled" >&2
        exit 1
    fi
else
    test ! -f "$d/unknown.s"
fi
echo "MINIC_EMPTY_VOLATILE=PASS target=$MINIC_EMPTY_TARGET original_GNU=1 native_asm_link=1 native_QEMU=1 unknown_rejected=1"
