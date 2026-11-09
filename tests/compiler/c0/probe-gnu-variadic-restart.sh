#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-${1:-"$root/build/release/bin/minic-cc"}}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
source="$root/tests/compiler/c0/gnu_variadic_restart_aapcs64.c"
driver="$root/tests/compiler/c0/gnu_variadic_restart_aapcs64_driver.c"
invalid="$root/tests/compiler/c0/gnu_variadic_restart_invalid.c"

for target in x86_64-linux aarch64-linux riscv32-linux riscv64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$source" -o "$work/$target.s"
    if "$minic" --target="$target" -S "$invalid" \
        -o "$work/$target-bad.s" >"$work/$target.out" 2>"$work/$target.err"; then
        echo "invalid nonvariadic va_start accepted on $target" >&2
        exit 1
    fi
    test ! -s "$work/$target-bad.s"
done

# Distinct AAPCS64 tags are allocated per source va_start. The register-save
# bank itself is immutable and shared. The second restart is not an alias of
# the first cursor's mutable GP/FP offsets.
awk '
    function begin_cursor(reg, offset) {
        cursor[reg] = offset
        next_field[reg] = 0
        first_line[reg] = NR
    }
    $0 == "mini_va_restart:" { within = 1; found = 1; next }
    within && /^\.size mini_va_restart,/ { within = 0; done = 1 }
    !within { next }
    # The AAPCS64 tag consists of three full-width pointers at byte offsets
    # 0/8/16 and two signed word offsets at 24/28. Establish two separately
    # allocated, fully initialized 32-byte tags, not a fixed x9/x10 pattern.
    /^  movz x[0-9]+, #[0-9]+(, lsl #[0-9]+)?$/ {
        line = $0; sub(/^ +/, "", line)
        split(line, fields, /[ ,#]+/)
        wide[fields[2]] = fields[3] ":" fields[5]
        next
    }
    /^  movk x[0-9]+, #[0-9]+, lsl #[0-9]+$/ {
        line = $0; sub(/^ +/, "", line)
        split(line, fields, /[ ,#]+/)
        wide[fields[2]] = wide[fields[2]] ":" fields[3] ":" fields[5]
        next
    }
    /^  add x[0-9]+, (sp|x[0-9]+), #[0-9]+$/ {
        line = $0; sub(/^ +/, "", line)
        split(line, fields, /[ ,#]+/)
        begin_cursor(fields[2], fields[3] ":" fields[4])
        next
    }
    /^  add x[0-9]+, (sp|x[0-9]+), x[0-9]+$/ {
        line = $0; sub(/^ +/, "", line)
        split(line, fields, /[ ,#]+/)
        if (fields[2] == fields[4] && wide[fields[4]] != "")
            begin_cursor(fields[2], fields[3] ":" wide[fields[4]])
        next
    }
    /^  str [xw][0-9]+, \[x[0-9]+(, #[0-9]+)?\]$/ {
        line = $0; sub(/^ +/, "", line)
        split(line, fields, /[ ,\[\]#]+/)
        base = fields[3]; offset = fields[4] + 0
        stage = next_field[base]
        if (cursor[base] == "" || stage < 0 || stage > 4 ||
            NR - first_line[base] > 18) next
        expected = stage < 3 ? stage * 8 : 24 + (stage - 3) * 4
        width = substr(fields[2], 1, 1)
        if (offset != expected ||
            (stage < 3 && width != "x") ||
            (stage >= 3 && width != "w")) next
        next_field[base]++
        if (next_field[base] == 5) {
            completed[cursor[base]] = 1
            next_field[base] = -1
        }
    }
    END {
        for (slot in completed) unique++
        if (!found || !done || unique != 2) {
            print "AAPCS64_VA_RESTART=FAIL initialized_distinct_tags=" unique > "/dev/stderr"
            exit 1
        }
    }
' "$work/aarch64-linux.s"

cc -O0 -no-pie "$source" "$driver" -o "$work/gnu-native"
cc -O0 -no-pie "$work/x86_64-linux.s" "$driver" -o "$work/minic-native"
"$work/gnu-native"
"$work/minic-native"

echo 'MINIC_GNU_VA_RESTART=PASS five_isa=5 AAPCS64_independent_cursors=2 GNU_native=1 MiniC_x86_native=1 nonvariadic_rejected=5'
