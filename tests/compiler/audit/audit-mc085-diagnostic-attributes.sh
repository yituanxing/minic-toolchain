#!/bin/sh
# Evidence-only MC-085 oracle. This intentionally does not require MiniC to
# match GCC: the current missing diagnostic consumer is an OPEN compiler bug.
# Usage: sh tests/compiler/audit/audit-mc085-diagnostic-attributes.sh [minic-cc]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${1:-"$root/build/release/bin/minic-cc"}
source="$root/tests/compiler/audit/mc085_diagnostic_attribute_cases.c"
scratch=$(mktemp -d "${TMPDIR:-/tmp}/minic-mc085-XXXXXX")
trap 'rm -rf -- "$scratch"' EXIT HUP INT TERM

if [ ! -x "$mini" ]; then
    echo "MiniC executable missing: $mini" >&2
    exit 2
fi

# O0/O2: direct live error; dead branch; constant; warning; indirect #1/#2;
# asm alias; always-inline guard; unused static; referenced static;
# paired error+warning; live condition; safe/attributed same-symbol aliases;
# cold label in if(0); and a source label jumped over unconditionally.
expected_O0='1 0 0 0 0 0 1 1 1 1 1 1 0 1 0 0'
expected_O2='1 0 0 0 1 1 1 0 0 1 1 1 0 1 0 0'

echo 'case GNU-O0 GNU-O2 MiniC-x86  [MC-085: MiniC mismatch is expected]'
case_id=1
set -- $expected_O0
for expect_O0 do
    expect_O2=$(printf '%s\n' "$expected_O2" | cut -d' ' -f"$case_id")
    gcc -std=gnu11 -E -P -DMC085_CASE="$case_id" "$source" \
        -o "$scratch/case_$case_id.i"
    for optimization in 0 2; do
        expected="$expect_O0"
        [ "$optimization" = 0 ] || expected="$expect_O2"
        if gcc -std=gnu11 -O"$optimization" -Wall \
            -Wno-unused-function -Wno-unused-label -S \
            -DMC085_CASE="$case_id" "$source" \
            -o "$scratch/gnu_${case_id}_${optimization}.s" \
            2>"$scratch/gnu_${case_id}_${optimization}.log"; then
            actual=0
        else
            actual=1
        fi
        if [ "$actual" != "$expected" ]; then
            echo "GNU oracle changed: case=$case_id O$optimization expected=$expected actual=$actual" >&2
            cat "$scratch/gnu_${case_id}_${optimization}.log" >&2
            exit 1
        fi
        # Only attribute-generated errors are expected; ordinary GCC errors
        # would invalidate the evidence, even with the same exit code.
        if [ "$expected" = 1 ] && ! grep -q 'attribute error: MC085' \
            "$scratch/gnu_${case_id}_${optimization}.log"; then
            echo "GNU unexpected failure case=$case_id O$optimization" >&2
            exit 1
        fi
        if { [ "$case_id" = 4 ] || [ "$case_id" = 11 ]; } && \
            ! grep -q 'attribute warning: MC085' \
                "$scratch/gnu_${case_id}_${optimization}.log"; then
            echo "GNU warning attribute disappeared: case=$case_id O$optimization" >&2
            exit 1
        fi
    done
    if "$mini" --target=x86_64-linux -S "$scratch/case_$case_id.i" \
        -o "$scratch/minic_$case_id.s" \
        2>"$scratch/minic_$case_id.log"; then
        mini_status=0
    else
        mini_status=1
    fi
    # The current MiniC baseline emits a syntactically unreachable CALL in
    # case 16 because source labels are machine-emission reachability roots.
    # This is precisely why that reachability set must not trigger diagnostics.
    if [ "$case_id" = 16 ] && [ "$mini_status" = 0 ] && \
        ! grep -q 'call stop@PLT' "$scratch/minic_$case_id.s"; then
        echo 'MC-085 case 16 emission behavior changed: re-audit diagnostic roots' >&2
        exit 1
    fi
    printf '%2d %6s %6s %10s\n' "$case_id" "$expect_O0" "$expect_O2" "$mini_status"
    case_id=$((case_id + 1))
done
echo 'MC-085 GNU O0/O2 oracle PASS; MiniC statuses reported without a pass assertion'
