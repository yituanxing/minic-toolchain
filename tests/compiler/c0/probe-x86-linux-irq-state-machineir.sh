#!/bin/sh
# Linux CLI/STI/STI-HLT: generated assembly only, never execute privileged
# instructions as a userspace runtime test. Strict GNU AS and bad variants.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
gnu=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/x86-irq-machineir
mkdir -p "$work"
cat >"$work/positive.c" <<'SOURCE'
void native_irq_disable(void) { asm volatile("cli" ::: "memory"); }
void native_irq_enable(void) { asm volatile("sti" ::: "memory"); }
void native_safe_halt(void) { asm volatile("sti; hlt" ::: "memory"); }
SOURCE
"$gnu" -std=gnu11 -E -P -x c "$work/positive.c" -o "$work/positive.i"
"$gnu" -c -x c "$work/positive.c" -o "$work/gnu.o"
"$minic" --target=x86_64-linux -S "$work/positive.i" -o "$work/minic.s"
"$gnu" -c -x assembler "$work/minic.s" -o "$work/minic.o"
for mnemonic in cli sti hlt; do
    grep -Eq "^[[:space:]]*$mnemonic$" "$work/minic.s"
done
for variant in omit-memory extra-instruction wrong-opcode; do
    case "$variant" in
        omit-memory) sed 's/"cli" ::: "memory"/"cli" :::/' "$work/positive.i" >"$work/negative.i" ;;
        extra-instruction) sed 's/"cli"/"cli; int3"/' "$work/positive.i" >"$work/negative.i" ;;
        wrong-opcode) sed 's/"cli"/"pushf"/' "$work/positive.i" >"$work/negative.i" ;;
    esac
    rm -f "$work/negative.s"
    if "$minic" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.s" >"$work/negative.out" 2>"$work/negative.err"; then
        echo "X86_IRQ_STATE_MACHINEIR=FAIL admitted=$variant" >&2; exit 1
    fi
    test ! -e "$work/negative.s"
done
printf '%s\n' 'X86_IRQ_STATE_MACHINEIR=PASS forms=3 GNU_AS=1 fail_closed=3 privileged_execution=NOT_RUN'
