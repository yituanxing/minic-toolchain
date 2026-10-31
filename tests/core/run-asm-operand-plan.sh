#!/usr/bin/env bash
set -Eeuo pipefail

minic="${1:?pass built minic-cc}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
temp="$(mktemp -d)"
trap 'rm -rf "$temp"' EXIT

# Link the real production plan capture and Core publication functions. The
# unused backend emitter sections are GC'd, so the test doesn't manufacture
# fake target qualifiers or duplicate the implementation under test.
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Werror -ffunction-sections -fdata-sections \
    -I"$root/include" -I"$root/src" \
    "$root/tests/core/asm_operand_plan_test.c" \
    "$root/src/target/backend.c" \
    "$root/src/core/core_lower_asm_operands.c" \
    -Wl,--gc-sections -o "$temp/asm-plan-unit"
"$temp/asm-plan-unit"

# All five MiniC ISA ingress paths, three Linux GNU qualified forms, generic
# RV64 early and RV32 fail-closed, plus x86 native GNU/MiniC fault-table oracle.
bash "$root/tests/compiler/c0/probe-target-gnu-earlyclobber-transport.sh" "$minic"

# Preserve deterministic rejection identity under the same source-form ID
# used for a successful Core plan; no generic opaque ASM fallback is allowed.
bash "$root/tests/compiler/c0/probe-linux-core-asm-unclaimed-census.sh" "$minic"
echo 'TYPED_ASM_OPERAND_PLAN=PASS source_core_target=1 targets=5 linux_oracle=1 fail_closed=1'
