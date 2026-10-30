#!/bin/sh
# GNU preprocessing -> MiniC -S -> GNU AS/link/runtime, with GNU -O2 oracle.
# Both live invalid masks must remain diagnosed; no fake symbol definitions.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
source_file="$root/tests/compiler/c0/gnu_linux_static_field_guard.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$host" -E -P -std=gnu11 "$source_file" -o "$work/valid.i"
"$host" -std=gnu11 -O2 "$source_file" -o "$work/valid-gnu"
"$mini" --target=x86_64-linux -S "$work/valid.i" -o "$work/valid-minic.s"
if grep -Fq 'invalid_field_mask' "$work/valid-minic.s"; then
    echo 'STATIC_FIELD_GUARD=FAIL MiniC retained a valid-mask diagnostic' >&2
    exit 1
fi
"$host" -c -x assembler "$work/valid-minic.s" -o "$work/valid-minic.o"
"$host" -no-pie "$work/valid-minic.o" -o "$work/valid-minic"
"$work/valid-gnu"
"$work/valid-minic"

for variant in DYNAMIC INVALID; do
    case "$variant" in
        DYNAMIC) define=TEST_DYNAMIC_FIELD_GUARD ;;
        INVALID) define=TEST_INVALID_FIELD_GUARD ;;
    esac
    "$host" -E -P -std=gnu11 -D"$define" "$source_file" \
        -o "$work/$variant.i"
    if "$host" -std=gnu11 -O2 -D"$define" -c "$source_file" \
        -o "$work/$variant-gnu.o" >"$work/$variant-gnu.log" 2>&1; then
        echo "STATIC_FIELD_GUARD=FAIL GNU accepted $variant" >&2
        exit 1
    fi
    grep -Fq 'field mask must be constant and contiguous' "$work/$variant-gnu.log"
    "$mini" --target=x86_64-linux -S "$work/$variant.i" \
        -o "$work/$variant-minic.s"
    "$host" -c -x assembler "$work/$variant-minic.s" \
        -o "$work/$variant-minic.o"
    if ! nm -u "$work/$variant-minic.o" | grep -Fq 'invalid_field_mask'; then
        echo "STATIC_FIELD_GUARD=FAIL MiniC lost reachable $variant guard" >&2
        exit 1
    fi
done

# Frozen, real Linux i915_hwmon.i (supplied separately by the GNU-link artifact)
# must no longer reference its five FIELD_GET compile-time assertions.
if [ -n "${MINIC_X86_I915_HWMON_FROZEN_I:-}" ]; then
    test -r "$MINIC_X86_I915_HWMON_FROZEN_I"
    "$mini" --target=x86_64-linux -S "$MINIC_X86_I915_HWMON_FROZEN_I" \
        -o "$work/i915-hwmon.s"
    "$host" -c -x assembler "$work/i915-hwmon.s" -o "$work/i915-hwmon.o"
    if nm -u "$work/i915-hwmon.o" | \
        grep -E '__compiletime_assert_(461|462|463|464|465)'; then
        echo 'STATIC_FIELD_GUARD=FAIL real Linux i915 FIELD_GET assertion' >&2
        exit 1
    fi
fi
printf 'STATIC_FIELD_GUARD=PASS GNU_O2=1 MiniC_S=1 GNU_AS=1 native_runtime=2 strict_negative=2 linux_i915=%s\n' \
    "${MINIC_X86_I915_HWMON_FROZEN_I:+checked}"
