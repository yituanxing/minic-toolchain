#!/usr/bin/env bash
# Exact AArch64 Linux privileged D/I-cache ISA: prove target-owned emission,
# independent GNU assembler acceptance, and fail-closed negative templates.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
mini=${1:-${MINIC_CC:-"$root/build/release/bin/minic-cc"}}
cc=${AARCH64_CC:-aarch64-linux-gnu-gcc}
objdump=${AARCH64_OBJDUMP:-aarch64-linux-gnu-objdump}
source="$root/tests/compiler/c0/aarch64_linux_cache_maintenance.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'AARCH64_CACHE=FAIL %s\n' "$*" >&2; exit 1; }

test -x "$mini" || fail 'MiniC compiler not executable'
test -s "$source" || fail 'cache-maintenance fixture absent'
gcc -E -P -std=gnu11 -x c "$source" -o "$work/cache.i"
"$mini" --target=aarch64-linux -S "$work/cache.i" -o "$work/cache.s" \
    || fail 'Linux cache-maintenance source rejected'
test -s "$work/cache.s" || fail 'MiniC emitted no target assembly'

# Count original hardware instructions, not mere Core admissions. AArch64
# DC/IC cache instructions are not user-mode QEMU smoke-test candidates.
for family in 'dc ivac' 'dc cvac' 'dc cvau' 'dc civac' 'ic ivau'; do
    count=$(grep -cF "  $family, x9" "$work/cache.s" || true)
    case "$family" in
        'dc civac') (( count >= 2 )) ||
            fail "required repeated target hardware instruction $family" ;;
        *) (( count >= 1 )) || fail "missing hardware instruction $family" ;;
    esac
done
grep -Fxq '  ic ialluis' "$work/cache.s" ||
    fail 'implicit-volatile I-cache-wide invalidation missing'

# The exact template and constraints are the hardware security boundary.
# Mutations below may be valid GNU assembler inputs, but are intentionally
# outside the frozen-original target contract. None may leave partial .s.
sed 's/dc civac, %0/dc madeup, %0/g' "$source" >"$work/unknown.c"
sed 's/dc civac, %0/dc civac, %1/g' "$source" >"$work/other-operand.c"
sed 's/"r" (address) : "memory"/"r" (address) : "cc"/g' \
    "$source" >"$work/no-memory.c"
sed 's/asm("ic\\tialluis")/asm("ic\\tialluis\\nret")/' \
    "$source" >"$work/hidden-instruction.c"
sed 's/dc civac, %0/dc civac, %0 ; nop/g' \
    "$source" >"$work/second-instruction.c"
sed 's/"r" (address)/"m" (address)/g' \
    "$source" >"$work/memory-constraint.c"

for variant in unknown other-operand no-memory hidden-instruction \
               second-instruction memory-constraint; do
    cmp -s "$source" "$work/$variant.c" &&
        fail "mutation absent: $variant"
    gcc -E -P -std=gnu11 -x c "$work/$variant.c" \
        -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
         -o "$work/$variant.s" >"$work/$variant.out" \
         2>"$work/$variant.err"; then
        fail "unsafe/unknown ISA accepted: $variant"
    fi
    test ! -e "$work/$variant.s" ||
        fail "partial assembly emitted for $variant"
done

if "$mini" --target=x86_64-linux -S "$work/cache.i" \
    -o "$work/foreign.s" >"$work/foreign.out" \
    2>"$work/foreign.err"; then
    fail 'privileged AArch64 ISA accepted as generic x86 ASM'
fi
test ! -e "$work/foreign.s" ||
    fail 'foreign backend left a partial assembly file'
printf '%s\n' 'AARCH64_CACHE_SOURCE=PASS exact_hardware=6 operands=typed negative=7 foreign_target=closed'

if ! command -v "$cc" >/dev/null 2>&1 ||
   ! command -v "$objdump" >/dev/null 2>&1; then
    if [[ ${MINIC_REQUIRE_AARCH64_CACHE:-0} == 1 ]]; then
        fail 'required independent GNU AArch64 assembler/objdump missing'
    fi
    printf '%s\n' 'AARCH64_CACHE_GNU_AS=SKIP GNU AArch64 tools unavailable'
    exit 0
fi

"$cc" -c -O0 -x c "$source" -o "$work/gnu.o" \
    || fail 'original GCC code rejected'
"$cc" -c -x assembler "$work/cache.s" -o "$work/mini.o" \
    || fail 'MiniC generated hardware code rejected by GNU GAS'
"$objdump" -dr "$work/gnu.o" >"$work/gnu.dump"
"$objdump" -dr "$work/mini.o" >"$work/mini.dump"
for family in 'dc ivac' 'dc cvac' 'dc cvau' 'dc civac' 'ic ivau' 'ic ialluis'; do
    # GNU disassemblers may print tabs/spaces between operands; normalize
    # those only. The opcode and instruction family must be present in BOTH.
    for producer in gnu mini; do
        grep -Eq "[[:space:]]${family// /[[:space:]]+}" \
            "$work/$producer.dump" ||
            fail "$producer missing real disassembled ISA family: $family"
    done
done
printf '%s\n' 'AARCH64_CACHE_GNU_AS=PASS original_GNU=1 MiniC_GNU_GAS=1 objdump_ISA=6 no_unprivileged_execution=1'
