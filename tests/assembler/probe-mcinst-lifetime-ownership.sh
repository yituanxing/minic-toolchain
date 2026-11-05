#!/usr/bin/env bash
# Capacity and destructor callbacks own all typed MCInst allocations.
# Reject the pre-migration count fallback and implicit free() policy.
set -Eeuo pipefail
src=assembler/src/mc_operand_semantics.c
grep -q 'slots = inst->capacity;' "$src"
! grep -q 'inst->capacity != 0U ? inst->capacity : inst->count' "$src"
grep -q 'm->dispose_target_payload = free;' assembler/src/riscv_encode.c
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -Iassembler/src -std=c11 -Wall -Wextra -Wpedantic -Werror \
   assembler/src/mc_operand_semantics.c \
   tests/assembler/mcinst-lifetime-contract.c \
   -o "$work/mcinst-lifetime"
"$work/mcinst-lifetime"
