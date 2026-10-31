#!/bin/sh
# LoongArch LP64D: an unnamed align-16 __int128 skips an odd GAR, unlike
# a named wide scalar. Direct and indirect calls share the ABI planner.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
caller=tests/compiler/c0/loongarch64_vararg_int128_pair.c
provider=tests/compiler/c0/loongarch64_vararg_int128_pair_gnu.c
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cc -E -P -x c "$caller" -o "$work/caller.i"
"$MINIC_CC" --target=loongarch64-linux -S "$work/caller.i" \
    -o "$work/caller.s"
test -s "$work/caller.s"

# Inspect the actual outgoing call register loads, not merely a successful
# -S. No other register load in these small forwarding wrappers uses a2/a3.
check_pair() {
    awk -v name="$1" -v first="$2" -v second="$3" '
        $0 == name ":" { active=1; found=1; next }
        active && $0 ~ "^\\.size "name"," { active=0 }
        active && $0 ~ "^[[:space:]]+ld\\.d \\$a"first"," { gotfirst++ }
        active && $0 ~ "^[[:space:]]+ld\\.d \\$a"second"," { gotsecond++ }
        END { if (!found || !gotfirst || !gotsecond) exit 1 }
    ' "$work/caller.s" || {
        echo "LOONG_VA_INT128=FAIL $1 expected_pair=a$2/a$3" >&2
        return 1
    }
}
check_no_a7() {
    awk -v name="$1" '
        $0 == name ":" { active=1; found=1; next }
        active && $0 ~ "^\\.size "name"," { active=0 }
        active && /^[[:space:]]+ld\.d \$a7,/ { bad=1 }
        END { if (!found || bad) exit 1 }
    ' "$work/caller.s" || {
        echo "LOONG_VA_INT128=FAIL $1 unexpected_gar=a7" >&2
        return 1
    }
}
check_stack_three() {
    awk -v name="$1" '
        $0 == name ":" { active=1; found=1; next }
        active && $0 ~ "^\\.size "name"," { active=0 }
        active && /^  li.d \$t3, 32$/ { start=1 }
        active && start && /^  sub.d \$sp, \$sp, \$t3$/ { window=1 }
        # The target-owned frame helper may use the equivalent single
        # signed-imm12 SP decrement; validate ABI slots, not opcode count.
        active && /^  addi.d \$sp, \$sp, -32$/ { window=1 }
        active && window && /^  st.d \$t0, \$sp, 0$/ { zero=1 }
        active && window && /^  st.d \$t0, \$sp, 8$/ { eight=1 }
        active && window && /^  st.d \$t0, \$sp, 16$/ { sixteen=1 }
        active && window && (/^  bl / || /^  jirl /) {
            if (!zero || !eight || !sixteen) bad=1
            call=1
            window=0
        }
        END { if (!found || !call || bad) exit 1 }
    ' "$work/caller.s" || {
        echo "LOONG_VA_INT128=FAIL $1 expected_stack=0,8,16" >&2
        return 1
    }
}
check_named_split() {
    awk '
        $0 == "loong_named_split_control:" { active=1; found=1; next }
        active && /^\.size loong_named_split_control,/ { active=0 }
        active && /^  ld.d \$a7,/ { a7=1 }
        active && /^  st.d \$t0, \$sp, 0$/ { spill=1 }
        active && /^  bl fixed_seven$/ { call=1 }
        END { if (!found || !a7 || !spill || !call) exit 1 }
    ' "$work/caller.s" || {
        echo 'LOONG_VA_INT128=FAIL named-a7-split-control' >&2
        return 1
    }
}
check_pair loong_va_pair_direct 2 3
check_pair loong_va_pair_indirect 2 3
check_no_a7 loong_va_spill_direct
check_no_a7 loong_va_spill_indirect
check_stack_three loong_va_spill_direct
check_stack_three loong_va_spill_indirect
check_pair loong_named_pair_control 1 2
check_named_split
echo 'LOONG_VA_INT128=PASS -S direct=aligned-a2a3 indirect=aligned-a2a3 a7-overflow=stack named=a1a2 named-a7=split'

# Optional independent GNU psABI/QEMU oracle: use GNU both as the C varargs
# callee (va_arg performs the register alignment) and for the caller control.
cross=
for candidate in loongarch64-linux-gnu-gcc-13 loongarch64-linux-gnu-gcc; do
    if command -v "$candidate" >/dev/null 2>&1; then
        cross=$candidate
        break
    fi
done
if [ -z "$cross" ] || ! command -v qemu-loongarch64 >/dev/null 2>&1; then
    echo 'LOONG_VA_INT128_ORACLE=NOT_RUN cross_gcc_or_qemu_unavailable'
    exit 0
fi
"$cross" -O0 -static "$caller" "$provider" -o "$work/gnu"
"$cross" -O0 -static -x assembler "$work/caller.s" -x c "$provider" \
    -o "$work/minic"
set +e
timeout 45s qemu-loongarch64 "$work/gnu"
gnu_status=$?
timeout 45s qemu-loongarch64 "$work/minic"
minic_status=$?
set -e
if [ "$gnu_status" -ne 42 ] || [ "$minic_status" -ne 42 ]; then
    echo "LOONG_VA_INT128_ORACLE=FAIL gnu=$gnu_status minic=$minic_status" >&2
    exit 1
fi
echo 'LOONG_VA_INT128_ORACLE=PASS GNU_GCC_QEMU_direct_indirect_named_a7=42'
