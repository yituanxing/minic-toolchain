#!/bin/sh
# Linux's original read_cpucfg expands to GCC's __cpucfg intrinsic. On
# LoongArch this must be physical CPUCFG, never an unresolved ELF call.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/c0/loongarch64_linux_cpucfg_intrinsic.c"

"${CC:-cc}" -E -P -x c "$source" -o "$work/original.i"
"$MINIC_CC" --target=loongarch64-linux -S "$work/original.i" \
    -o "$work/minic.s"
test "$(grep -Fc '  cpucfg $a0, $a0' "$work/minic.s")" -eq 3
if grep -Eq '(^|[^a-zA-Z0-9_])(__cpucfg|__minic_target_builtin_loongarch_cpucfg)([^a-zA-Z0-9_]|$)' \
        "$work/minic.s"; then
    echo 'LOONG_CPUCFG_INTRINSIC=FAIL unresolved intrinsic in assembly' >&2
    exit 1
fi
# The GCC intrinsic alias is a LoongArch-only target capability.
printf '%s\n' 'unsigned int bad(unsigned int x) { return __cpucfg(x); }' \
    >"$work/foreign.i"
if "$MINIC_CC" --target=x86_64-linux -S "$work/foreign.i" \
        -o "$work/foreign.s" >"$work/foreign.log" 2>&1; then
    echo 'LOONG_CPUCFG_INTRINSIC=FAIL leaked into x86 frontend' >&2
    exit 1
fi

cross=${LOONGARCH_GCC:-loongarch64-linux-gnu-gcc-13}
nm=${LOONGARCH_NM:-loongarch64-linux-gnu-nm}
if command -v "$cross" >/dev/null 2>&1; then
    command -v "$nm" >/dev/null 2>&1 || {
        echo "LOONG_CPUCFG_INTRINSIC=FAIL GNU nm unavailable: $nm" >&2
        exit 1
    }
    "$cross" -O2 -c -x c "$work/original.i" -o "$work/gnu.o"
    "$cross" -c -x assembler "$work/minic.s" -o "$work/minic.o"
    for side in gnu minic; do
        "$nm" -u "$work/$side.o" >"$work/$side.undefined"
        if grep -F '__cpucfg' "$work/$side.undefined"; then
            echo "LOONG_CPUCFG_INTRINSIC=FAIL $side undefined __cpucfg" >&2
            exit 1
        fi
    done
    echo 'LOONG_CPUCFG_INTRINSIC=PASS GNU/MiniC same-i GNU_AS=PASS nm_u_cpucfg=ABSENT'
else
    echo 'LOONG_CPUCFG_INTRINSIC=PASS MiniC native_cpucfg=3 GNU_AS=NOT_RUN cross_toolchain_missing'
fi

# Optional full original GNU-preprocessed Linux owner, preserved by the
# immutable frozen GNU Kbuild producer (e.g. arch/loongarch/kernel/traps.i).
# This must be the same bytes on both compiler sides, never a synthetic
# replacement or altered Linux source. It does not change the base gate.
if [ -n "${MINIC_CPUCFG_FROZEN_I:-}" ]; then
    test -f "$MINIC_CPUCFG_FROZEN_I"
    "$MINIC_CC" --target=loongarch64-linux -S \
        "$MINIC_CPUCFG_FROZEN_I" -o "$work/frozen.minic.s"
    grep -Fq '  cpucfg $a0, $a0' "$work/frozen.minic.s"
    if grep -F '__cpucfg' "$work/frozen.minic.s"; then
        echo 'LOONG_CPUCFG_FROZEN=FAIL external __cpucfg in original GNU owner' >&2
        exit 1
    fi
    if command -v "$cross" >/dev/null 2>&1; then
        "$cross" -O2 -c -x c "$MINIC_CPUCFG_FROZEN_I" \
            -o "$work/frozen.gnu.o"
        "$cross" -c -x assembler "$work/frozen.minic.s" \
            -o "$work/frozen.minic.o"
        for side in gnu minic; do
            "$nm" -u "$work/frozen.$side.o" >"$work/frozen.$side.undefined"
            if grep -F '__cpucfg' "$work/frozen.$side.undefined"; then
                echo "LOONG_CPUCFG_FROZEN=FAIL $side undefined __cpucfg" >&2
                exit 1
            fi
        done
        echo 'LOONG_CPUCFG_FROZEN=PASS original_gnu_i GNU_AS=PASS nm_u_cpucfg=ABSENT'
    else
        echo 'LOONG_CPUCFG_FROZEN=PASS original_gnu_i native_cpucfg=1 GNU_AS=NOT_RUN'
    fi
fi
