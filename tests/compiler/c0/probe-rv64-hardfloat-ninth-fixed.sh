#!/bin/sh
# Official RV64 LP64D FP ABI: fa0..fa7, then next available integer GAR,
# then the stack; the FP and GP streams are independent. Both direct and
# function-pointer calls must use the same prepared ABI plan.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

${CC:-cc} -E -P -x c "$root/tests/compiler/c0/rv64_hardfloat_ninth_fixed.c" \
    -o "$work/ninth.i"
"$MINIC_CC" --target=riscv64-linux -march=rv64imafd -mabi=lp64d -S \
    "$work/ninth.i" -o "$work/hard.s"
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 -S \
    "$work/ninth.i" -o "$work/soft.s"

# Extract function-level properties without conflating the caller's eight
# outgoing fa moves with the callee's eight incoming fmv.x.d transfers.
for spec in ninth:a0:8 tenth_mixed:a2:8 spill_mixed:STACK:8 \
            ninth_float:a0:8; do
    name=${spec%%:*}
    rest=${spec#*:}
    last_gp=${rest%%:*}
    expected_fp=${rest#*:}
    awk -v name="$name" -v gp="$last_gp" -v expected="$expected_fp" '
        $0 == name ":" { inside = 1; found = 1; next }
        inside && index($0, ".size " name ",") == 1 { inside = 0; done = 1 }
        inside && $0 ~ /^  fmv\.x\.d t0, fa[0-7]$/ { fprs++ }
        inside && $0 == "  mv t0, " gp { gp_found++ }
        inside && $0 ~ /^  ld t0, [0-9]+\(sp\)$/ { stack_load++ }
        END {
            if (!found || !done || fprs != expected ||
                (gp != "STACK" && gp_found == 0) ||
                (gp == "STACK" && stack_load == 0)) {
                printf "RV64_HARDFLOAT_NINTH=FAIL %s fprs=%d expected=%d gp=%d stack=%d\n", \
                    name, fprs, expected, gp_found, stack_load > "/dev/stderr"
                exit 1
            }
        }
    ' "$work/hard.s"
done

for spec in 'call_ninth:  ld a0, ' \
            'call_tenth_mixed:  ld a2, ' \
            'call_spill_mixed:  sd t0, 0(sp)' \
            'call_ninth_float:  ld a0, ' \
            'call_indirect_ninth:  jalr ra, t0, 0'; do
    name=${spec%%:*}
    expected=${spec#*:}
    awk -v name="$name" -v expected="$expected" '
        $0 == name ":" { inside = 1; found = 1; next }
        inside && index($0, ".size " name ",") == 1 { inside = 0; done = 1 }
        inside && index($0, expected) == 1 { proof++ }
        END { if (!found || !done || proof == 0) exit 1 }
    ' "$work/hard.s" || { echo "RV64_HARDFLOAT_NINTH=FAIL outgoing $name" >&2; exit 1; }
done

if grep -Eq '^  (fadd\.|fdiv\.|fmv\.|fsub\.|fmul\.)' "$work/soft.s"; then
    echo 'RV64_HARDFLOAT_NINTH=FAIL F/D in strict soft-float LP64' >&2
    exit 1
fi
echo 'RV64_HARDFLOAT_NINTH=PASS -S LP64D=9th/10th/mixed/stack/float/indirect LP64=softfloat'
