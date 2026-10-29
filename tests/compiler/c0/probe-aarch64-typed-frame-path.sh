#!/usr/bin/env bash
set -Eeuo pipefail
test "$#" -eq 1
minic="$1"
test -x "$minic"
repo="$(cd "$(dirname "$0")/../../.." && pwd)"
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

# Native API tests verify width encoding, live scratch masking, stable x19
# versus outgoing SP, incoming aligned frame SP and overflow rejection.
cc -std=c11 -Wall -Wextra -Werror -I"$repo/src" \
    "$repo/src/target/aarch64/frame_emit.c" \
    "$repo/tests/target/aarch64/frame_emit_test.c" \
    -o "$scratch/frame-test"
"$scratch/frame-test"

# Real production paths: Core FP SSA and scalar memory operations.
"$minic" --target=aarch64-linux -S \
    "$repo/tests/compiler/c0/aarch64-typed-frame-production.i" \
    -o "$scratch/real.s"
grep -Fq 'fadd d9, d9, d10' "$scratch/real.s"
grep -Fq 'fmul d9, d9, d10' "$scratch/real.s"
grep -Fq 'fdiv d9, d9, d10' "$scratch/real.s"
grep -Eq 'ldr x10, \[(sp|x19)' "$scratch/real.s"
grep -Fq 'str x9, [x10]' "$scratch/real.s"

# Real Core block/return/parameter paths, including a moving live SP and
# record-copy returns. These now share the same typed owned-slot/frame APIs
# as FP and memory rather than depending on a post-emission SP string pass.
"$minic" --target=aarch64-linux -S \
    "$repo/tests/compiler/c0/dynamic_stack_alloca.c" \
    -o "$scratch/dynamic.s"
grep -Fq '  mov x19, sp' "$scratch/dynamic.s"
grep -Eq '^  (ldr|str) [xw]9, \[x19' "$scratch/dynamic.s"
# Within an alloca frame, outgoing calls must read Core-owned arguments from
# the stable x19 frame and write only actual ABI spill slots at live SP.
awk '
    /^  sub sp, sp, #16$/ { call=1 }
    call && /^  ldr x9, \[x19, #[0-9]+\]$/ { owned=1 }
    call && /^  str x9, \[sp, #0\]$/ { outgoing0=1 }
    call && /^  str x9, \[sp, #8\]$/ { outgoing1=1 }
    call && /^  bl dynamic_stack_sum10$/ { invoked=1 }
    call && /^  add sp, sp, #16$/ { restored=1; exit }
    END { exit !(call && owned && outgoing0 && outgoing1 && invoked && restored) }
' "$scratch/dynamic.s"
cc -E -P -x c \
    "$repo/tests/compiler/c0/aarch64_indirect_record_return_minic.c" \
    -o "$scratch/record.i"
"$minic" --target=aarch64-linux -S \
    "$scratch/record.i" -o "$scratch/record.s"
grep -Eq '^  ldr x30, \[(x19|x17)' "$scratch/record.s"
grep -Eq '^  (add x11, x19|mov x11, x19)' "$scratch/record.s"
# A >4 KiB owned frame uses typed scratch materialization, never a silent
# truncation to an A64 immediate or an x12/x17 string postpass guess.
grep -Eq '^  movz x17, #[4-9][0-9]{3,}$' "$scratch/record.s"
grep -Fq '  str x8, [x17]' "$scratch/record.s"
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c -x assembler "$scratch/real.s" \
        -o "$scratch/real.o"
    aarch64-linux-gnu-gcc -c -x assembler "$scratch/dynamic.s" \
        -o "$scratch/dynamic.o"
    aarch64-linux-gnu-gcc -c -x assembler "$scratch/record.s" \
        -o "$scratch/record.o"
fi
echo 'AARCH64_TYPED_FRAME_PRODUCTION=PASS fp_memory_block_return_dynamic_owned_outgoing_large_record_gnu_asm_if_available'
