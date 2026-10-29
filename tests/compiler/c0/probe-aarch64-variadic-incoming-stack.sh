#!/usr/bin/env bash
# AAPCS64: register exhaustion must start at the caller's first incoming
# stack argument, past the callee x29/x30 record, including realigned alloca.
set -Eeuo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host=${HOST_CC:-cc}
src="$root/tests/compiler/c0/aarch64_variadic_incoming_stack.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$host" -E -P -std=gnu11 -DMINIC_AARCH64_CURSOR "$src" \
    -o "$work/stack.i"
"$mini" --target=aarch64-linux -S "$work/stack.i" -o "$work/stack.s"
"$host" -std=gnu11 -O0 -Wno-unused-parameter "$src" -o "$work/host-oracle"
"$work/host-oracle" || {
    echo 'AARCH64_VA_INCOMING=FAIL host-semantics' >&2; exit 1;
}

check_static_stack_cursor() {
    local function=$1 named_bytes=$2 frame first_arg_offset
    frame=$(sed -n "/^${function}:/,/^\\.size ${function}/p" \
        "$work/stack.s" | sed -n 's/^  movz x12, #\([0-9]*\)$/\1/p' | head -n 1)
    test -n "$frame"
    first_arg_offset=$((frame + 16 + named_bytes))
    sed -n "/^${function}:/,/^\\.size ${function}/p" \
        "$work/stack.s" | awk -v expected="  add x10, sp, #$first_arg_offset" '
            $0 == "  str x10, [x9]" && previous == expected {found=1}
            {previous=$0}
            END {if (!found) exit 1}
        ' || {
        echo "AARCH64_VA_INCOMING=FAIL $function offset=$first_arg_offset" >&2
        exit 1
    }
}

check_dynamic_stack_cursor() {
    local function=$1 named_bytes=$2 first_arg_offset
    first_arg_offset=$((16 + named_bytes))
    sed -n "/^${function}:/,/^\\.size ${function}/p" \
        "$work/stack.s" | awk -v expected="  add x10, x17, #$first_arg_offset" '
            $0 == "  str x10, [x9]" && previous == expected &&
              previous2 ~ /^  ldr x17, \[x19, #[0-9]+\]$/ {found=1}
            {previous2=previous; previous=$0}
            END {if (!found) exit 1}
        ' || {
        echo "AARCH64_VA_INCOMING=FAIL $function saved-entry offset=$first_arg_offset" >&2
        exit 1
    }
}

# Prologue owns exactly 16 bytes for the x29/x30 frame record before it
# allocates the separate fixed local frame. The static va_list __stack
# pointer must therefore be SP+frame+16, never SP+frame.
check_static_stack_cursor static_stack_cursor 0
# The ninth named GP occupies one entry stack slot, so the first anonymous
# GP begins at entry-SP+24, not entry-SP+16.
check_static_stack_cursor named_gp_stack_cursor 8
# Named g8 and f8 each occupy an eight-byte caller stack slot, giving +32.
check_static_stack_cursor named_mixed_stack_cursor 16

# Two named 16-byte GP records use x0..x3 (four registers, not two
# parameters), so the saved GPR cursor starts at offset -32. movn #31
# materializes the exact 32-bit two's-complement value -32.
sed -n '/^named_record_gp_cursor:/,/^\.size named_record_gp_cursor/p' \
    "$work/stack.s" | awk '
        $0 == "  str w10, [x9, #24]" && previous == "  movn w10, #31" {found=1}
        {previous=$0}
        END {if (!found) exit 1}
    ' || {
    echo 'AARCH64_VA_INCOMING=FAIL named-record-two-registers-per-parameter' >&2
    exit 1
}

# With over-aligned dynamic allocation, x19 names a movable aligned frame;
# only the prologue-saved entry SP can locate the original stack arguments.
check_dynamic_stack_cursor aligned_dynamic_stack_cursor 0
check_dynamic_stack_cursor named_aligned_dynamic_stack_cursor 8

# With both named register banks exhausted, the AAPCS64 signed cursor
# offsets __gr_offs and __vr_offs must each be zero, never underflow.
sed -n '/^named_mixed_stack_cursor:/,/^\.size named_mixed_stack_cursor/p' \
    "$work/stack.s" | awk '
        $0 == "  movz w10, #0" {zero=1; next}
        $0 == "  str w10, [x9, #24]" && zero {gp=1; zero=0; next}
        $0 == "  str w10, [x9, #28]" && zero {fp=1; zero=0; next}
        {zero=0}
        END {if (!gp || !fp) exit 1}
    ' || {
    echo 'AARCH64_VA_INCOMING=FAIL exhausted-gp-fp-offsets' >&2; exit 1;
}

if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 &&
   command -v qemu-aarch64 >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -std=gnu11 -O0 -static "$src" -o "$work/gnu"
    aarch64-linux-gnu-gcc -static -x assembler "$work/stack.s" -o "$work/mini"
    timeout 30s qemu-aarch64 "$work/gnu"
    timeout 30s qemu-aarch64 "$work/mini"
    echo 'AARCH64_VA_INCOMING=PASS named_stack_gp=1 named_stack_fp=1 named_record_gp=1 dynamic_saved_entry=1 host_oracle=PASS GNU_QEMU=PASS'
else
    echo 'AARCH64_VA_INCOMING=PASS named_stack_gp=1 named_stack_fp=1 named_record_gp=1 dynamic_saved_entry=1 host_oracle=PASS GNU_QEMU=SKIP'
fi
