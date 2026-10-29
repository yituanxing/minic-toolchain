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
cc -E -P -x c \
    "$repo/tests/compiler/c0/aarch64_indirect_record_return_minic.c" \
    -o "$scratch/record.i"
"$minic" --target=aarch64-linux -S \
    "$scratch/record.i" -o "$scratch/record.s"
grep -Eq '^  ldr x30, \[(x19|x17)' "$scratch/record.s"
grep -Eq '^  (add x11, x19|mov x11, x19)' "$scratch/record.s"
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c -x assembler "$scratch/real.s" \
        -o "$scratch/real.o"
    aarch64-linux-gnu-gcc -c -x assembler "$scratch/dynamic.s" \
        -o "$scratch/dynamic.o"
    aarch64-linux-gnu-gcc -c -x assembler "$scratch/record.s" \
        -o "$scratch/record.o"
fi
echo 'AARCH64_TYPED_FRAME_PRODUCTION=PASS fp_memory_block_return_dynamic_record_gnu_asm_if_available'
