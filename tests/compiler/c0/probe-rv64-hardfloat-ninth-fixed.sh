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
if "$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64d -S \
    "$work/ninth.i" -o "$work/invalid-hard.s" 2>"$work/invalid-hard.err"; then
    echo 'RV64_HARDFLOAT_NINTH=FAIL LP64D accepted ISA without F/D' >&2
    exit 1
fi
if ! grep -Fq 'lp64d requires F/D' "$work/invalid-hard.err"; then
    echo 'RV64_HARDFLOAT_NINTH=FAIL wrong LP64D ISA rejection' >&2
    exit 1
fi

# Extract function-level properties without conflating the caller's eight
# outgoing fa moves with the callee's eight incoming fmv.x.d transfers.
for spec in ninth:a0:8 tenth_mixed:a2:8 spill_mixed:STACK:8 \
            ninth_float:a0:8; do
    name=${spec%%:*}
    rest=${spec#*:}
    last_gp=${rest%%:*}
    expected_fp=${rest#*:}
    awk -v name="$name" -v gp="$last_gp" -v expected="$expected_fp" '
        BEGIN {
            # Typed integer-machine serialization uses architectural xN;
            # native FP instructions retain GNU faN/ftN ABI aliases.
            if (gp == "a0") gp = "x10"
            else if (gp == "a2") gp = "x12"
        }
        $0 == name ":" { inside = 1; found = 1; next }
        inside && index($0, ".size " name ",") == 1 { inside = 0; done = 1 }
        inside && $0 ~ /^  fmv\.x\.d t0, fa[0-7]$/ { fprs++ }
        inside && $0 == "  mv x5, " gp { gp_found++ }
        inside && $0 ~ /^  ld x5, [0-9]+\(x2\)$/ { stack_load++ }
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

for spec in 'call_ninth:  ld x10, ' \
            'call_tenth_mixed:  mv x12, x5' \
            'call_spill_mixed:  sd x5, 0(x2)' \
            'call_ninth_float:  ld x10, ' \
            'call_indirect_ninth:  jalr ra, 0(x5)'; do
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

# Both strict LP64 and LP64D require the identical integer-only typed path.
# A previous shared switch sent the integer family into native-FPR selection
# whenever the ABI was LP64D, even though the Core operands were integers.
for flavor in hard soft; do
    for name in int32_arithmetic int64_arithmetic; do
        if test "$name" = int32_arithmetic; then
            div_symbol=__udivdi3
            rem_symbol=__umoddi3
        else
            div_symbol=__divdi3
            rem_symbol=__moddi3
        fi
        awk -v name="$name" -v div_symbol="$div_symbol" \
            -v rem_symbol="$rem_symbol" '
            $0 == name ":" { inside = 1; found = 1; next }
            inside && index($0, ".size " name ",") == 1 { inside = 0; done = 1 }
            inside && /__muldi3/ { mul++ }
            inside && index($0, div_symbol) { div++ }
            inside && index($0, rem_symbol) { rem++ }
            inside && /^  (fadd\.|fdiv\.|fmv\.|fsub\.|fmul\.)/ { invalid_fp++ }
            END {
                if (!found || !done || mul == 0 || div == 0 ||
                    rem == 0 || invalid_fp != 0) exit 1
            }
        ' "$work/$flavor.s" || {
            echo "RV64_HARDFLOAT_NINTH=FAIL typed integer call $flavor $name" >&2
            exit 1
        }
    done
done
echo 'RV64_HARDFLOAT_NINTH=PASS -S LP64D=9th/10th/mixed/stack/float/indirect LP64=softfloat integer_MUL_DIV_REM=LP64+LP64D no-D=reject'
