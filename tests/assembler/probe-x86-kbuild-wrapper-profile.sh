#!/usr/bin/env bash
# Verify the exact Linux Kbuild GAS interposer selects different, explicit
# Target object ABIs for GCC's --32 and --64 subprocess invocations.
set -Eeuo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
minias=${MINIAS:-"$root/build/release/bin/minic-as"}
work=${WORK_DIR:-"$(mktemp -d)"}
mkdir -p "$work/failure-inputs"
src="$root/tests/external/linux/minias_x86_64_linux.sh"
awk '
    index($0,"cat > \"$wrapper\" <<")==1 {capture=1; next}
    capture && $0=="WRAPPER" {exit}
    capture {print}
' "$src" >"$work/as"
test -s "$work/as"
chmod +x "$work/as"
bash -n "$work/as"
cat >"$work/test.s" <<'ASM'
.text
.globl x86_kbuild_mode
x86_kbuild_mode:
    .byte 0x90
.data
    .long external_symbol+4
ASM

: >"$work/trace"
for bitness in 32 64; do
    MINIAS="$minias" MINIAS_AS_TRACE="$work/trace" \
    MINIAS_FAILURE_DIR="$work/failure-inputs" REAL_AS="$(command -v as)" \
        "$work/as" "--$bitness" -o "$work/$bitness.o" "$work/test.s"
done
readelf -h "$work/32.o" | grep -q 'Class:.*ELF32'
readelf -h "$work/32.o" | grep -q 'Machine:.*Intel 80386'
readelf -S "$work/32.o" | grep -q '\.rel\.data'
readelf -r "$work/32.o" | grep -q 'R_386_32'
readelf -h "$work/64.o" | grep -q 'Class:.*ELF64'
readelf -h "$work/64.o" | grep -q 'Machine:.*X86-64'
readelf -S "$work/64.o" | grep -q '\.rela\.data'
readelf -r "$work/64.o" | grep -q 'R_X86_64_32'
test "$(grep -c '^pass ' "$work/trace")" -eq 2
if grep -q '^fail' "$work/trace"; then
    echo 'x86 wrapper recorded a failed production invocation' >&2
    exit 1
fi
echo 'MINIAS_X86_KBUILD_WRAPPER=PASS ELF32_REL=1 ELF64_RELA=1 mode_separate=1 no_gnu_gas_fallback=1'
