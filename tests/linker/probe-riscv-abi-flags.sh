#!/bin/sh
# Host-runnable ABI producer/consumer test. GNU RISC-V tools are not needed:
# MiniAS produces real ELF64 RISC-V objects and MiniLD merges them with -r.
set -eu

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-riscv-abi.XXXXXX")
trap 'rm -r -- "$work"' EXIT HUP INT TERM

cat >"$work/first.s" <<'ASM'
.text
.globl first
.type first,@function
first:
    ret
.size first,.-first
ASM

cat >"$work/second.s" <<'ASM'
.text
.globl second
.type second,@function
second:
    ret
.size second,.-second
ASM

cat >"$work/data-only.s" <<'ASM'
.data
.globl data_only
.type data_only,@object
data_only:
    .word 9
.size data_only,4
ASM

"$MINIAS" -march=rv64imaf -mabi=lp64f \
    -o "$work/first-f.o" "$work/first.s"
"$MINIAS" -march=rv64imafc -mabi=lp64f \
    -o "$work/second-fc.o" "$work/second.s"
"$MINIAS" -march=rv64imafd -mabi=lp64d \
    -o "$work/second-d.o" "$work/second.s"
"$MINIAS" -march=rv64imafd -mabi=lp64d \
    -o "$work/data-only-d.o" "$work/data-only.s"

# Both objects use single-float ABI; the C extension is union-compatible.
"$MINILD" -melf64lriscv -r -o "$work/compatible.o" \
    "$work/first-f.o" "$work/second-fc.o"
readelf -h "$work/compatible.o" >"$work/compatible.header"
grep -Eq 'Flags:[[:space:]]+0x3([,[:space:]]|$)' "$work/compatible.header"

# A later data-only object does not override the first code object's ABI,
# matching the GNU BFD special case.
"$MINILD" -melf64lriscv -r -o "$work/data-only.o" \
    "$work/first-f.o" "$work/data-only-d.o"
readelf -h "$work/data-only.o" >"$work/data-only.header"
grep -Eq 'Flags:[[:space:]]+0x2([,[:space:]]|$)' "$work/data-only.header"

# FLOAT_ABI=2 (LP64F) and =4 (LP64D) are mutually exclusive encodings.
# Bitwise-OR would silently create =6 (LP64Q), an incorrect ABI.
if "$MINILD" -melf64lriscv -r -o "$work/incompatible.o" \
    "$work/first-f.o" "$work/second-d.o" \
    >"$work/incompatible.stdout" 2>"$work/incompatible.stderr"; then
    echo 'MINILD_RISCV_ABI_INTEGRATION=FAIL incompatible-float-abi-accepted' >&2
    exit 1
fi
grep -q 'incompatible-riscv-elf-flags:.*float-abi-mismatch' \
    "$work/incompatible.stderr"
if [ -e "$work/incompatible.o" ]; then
    echo 'MINILD_RISCV_ABI_INTEGRATION=FAIL emitted-object-on-error' >&2
    exit 1
fi

echo 'MINILD_RISCV_ABI_INTEGRATION=PASS rvc-union=true float-conflict-rejected=true data-only-exempt=true'
