#!/bin/sh
# Original LoongArch64 Linux relocate.c updates file-scope register
# __current_thread_info asm("$tp"). Read and write must access LIVE TP;
# stack-pointer and arbitrary scratch-register assignments stay forbidden.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
src=tests/compiler/c0/loongarch64_linux_tp_write.c
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
gcc -E -P -nostdinc -x c "$src" -o "$work/relocate.i"
"$MINIC_CC" --target=loongarch64-linux -fPIE -S \
    "$work/relocate.i" -o "$work/relocate.s"
grep -Fq '  move $t0, $tp' "$work/relocate.s"
grep -Fq '  move $tp, $t0' "$work/relocate.s"
if grep -Eq '^[.]comm __current_thread_info|^__current_thread_info:' \
        "$work/relocate.s"; then
    echo 'MINIC_LOONG_TP=FAIL fabricated ELF object for live TP' >&2
    exit 1
fi
echo 'MINIC_LOONG_TP=PASS live_read=1 live_write=1 no_elf_object=1'

cross=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
if command -v "$cross" >/dev/null 2>&1; then
    "$cross" -fPIE -S "$src" -o "$work/gnu.s"
    "$cross" -fPIE -c -x assembler "$work/relocate.s" -o "$work/relocate.o"
    echo 'MINIC_LOONG_TP_GNU_AS=PASS original_GNU=1 MiniC_object=1'
else
    echo 'MINIC_LOONG_TP_GNU_AS=SKIP missing Loong GNU toolchain'
fi

for bad in stack scratch; do
    case "$bad" in
        stack) sed 's/"\$tp"/"\$sp"/' "$work/relocate.i" >"$work/invalid.i" ;;
        scratch) sed 's/"\$tp"/"\$t0"/' "$work/relocate.i" >"$work/invalid.i" ;;
    esac
    if "$MINIC_CC" --target=loongarch64-linux -fPIE -S \
        "$work/invalid.i" -o "$work/invalid.s" 2>"$work/invalid.err"; then
        echo "MINIC_LOONG_TP_NEGATIVE=FAIL $bad binding accepted" >&2
        exit 1
    fi
    test ! -e "$work/invalid.s"
done
echo 'MINIC_LOONG_TP_NEGATIVE=PASS stack_write=reject scratch_write=reject'
