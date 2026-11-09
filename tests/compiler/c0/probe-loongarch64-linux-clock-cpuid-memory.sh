#!/bin/sh
# Source-qualified CPU-ID exact memory clobber, Core MachineIR and fail-closed cases.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
input="$root/tests/compiler/c0/loongarch64_linux_clock_cpuid_memory.c"
cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$input"
cc -E -P -x c "$input" -o "$work/cpuid.i"
"$minic" --target=loongarch64-linux -mabi=lp64s -S "$work/cpuid.i" -o "$work/cpuid.s"
grep -Eq '^[[:space:]]+rdtime[.]d[[:space:]]+\$zero,[[:space:]]*\$t0$' "$work/cpuid.s"
for bad in missing-memory extra-clobber invalid-template; do
    case "$bad" in
        missing-memory)
            sed 's/: : "memory"/: :/' "$work/cpuid.i" > "$work/bad.i" ;;
        extra-clobber)
            sed 's/: : "memory"/: : "memory", "cc"/' "$work/cpuid.i" > "$work/bad.i" ;;
        invalid-template)
            sed 's/rdtime.d \$zero/rdtime.w $zero/' "$work/cpuid.i" > "$work/bad.i" ;;
    esac
    if cmp -s "$work/cpuid.i" "$work/bad.i"; then
        echo "LOONG_CPU_ID_CLOCK_NEGATIVE=FAIL missed_mutation=$bad" >&2
        exit 1
    fi
    if "$minic" --target=loongarch64-linux -mabi=lp64s -S \
        "$work/bad.i" -o "$work/bad.s" >"$work/out" 2>"$work/err"; then
        echo "LOONG_CPU_ID_CLOCK_NEGATIVE=FAIL admitted=$bad" >&2
        exit 1
    fi
    test ! -e "$work/bad.s"
done
echo 'LOONG_CPU_ID_CLOCK=PASS GNU_C=1 original_form=1 memory_clobber=1 negatives=3 GNU_AS=NOT_RUN'
