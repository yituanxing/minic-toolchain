#!/bin/sh
# Record loads must copy byte-exactly: source pointer alignment is not proven
# at Core RECORD_LOAD even when the record type is naturally aligned.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

${CC:-cc} -E -P -x c "$root/tests/compiler/c0/rv64_record_load_packed.c" \
    -o "$work/packed.i"
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 -S \
    "$work/packed.i" -o "$work/packed.s"

for item in packed_two:2 packed_four:4 packed_eight:8 packed_four_with_local:4 natural_two:2 natural_four:4 natural_eight:8 natural_field_packed:4; do
    suffix=${item%%:*}
    bytes=${item#*:}
    awk -v name="load_$suffix" -v expected="$bytes" '
        $0 == name ":" { within = 1; found = 1; next }
        within && index($0, ".size " name ",") == 1 { within = 0; done = 1 }
        # Physical MachineIR assigns x-registers, not the old fixed t0/t1/t2.
        # Every byte offset in the record must have a byte-read and byte-write
        # witness. Other byte accesses (e.g. return ABI gather) may coexist.
        within && $0 ~ /^  lbu x[0-9]+, [0-9]+\(x[0-9]+\)$/ {
            offset = $3; sub(/\(.*/, "", offset); reads[offset]++
        }
        within && $0 ~ /^  sb x[0-9]+, [0-9]+\(x[0-9]+\)$/ {
            offset = $3; sub(/\(.*/, "", offset); writes[offset]++
        }
        within && $0 ~ /^  addi x[0-9]+, x[0-9]+, 1$/ { member_offset++ }
        END {
            for (i = 0; i < expected; ++i) {
                if (!reads[i] || !writes[i]) missing++
            }
            if (!found || !done || missing ||
                (name == "load_natural_field_packed" && !member_offset)) {
                printf "RV64_ALIGN_SAFE_RECORD_LOAD=FAIL %s bytes=%d missing_offsets=%d member_offset=%d\n", \
                    name, expected, missing, member_offset > "/dev/stderr"
                exit 1
            }
        }
    ' "$work/packed.s"
done

# The first packed by-value parameter occupies frame byte zero; the second
# starts at byte one. Verify a stack+1 address and a byte-scatter/gather
# witness for every byte of the second record; a wide access through that
# unaligned address is not a permitted replacement.
for item in two:2 four:4 eight:8; do
    suffix=${item%%:*}
    bytes=${item#*:}
    awk -v name="mix_after_one_$suffix" -v expected="$bytes" '
        $0 == name ":" { within = 1; found = 1; next }
        within && index($0, ".size " name ",") == 1 { within = 0; done = 1 }
        within && $0 ~ /^  addi x[0-9]+, x2, 1$/ { odd_frame++ }
        within && $0 ~ /^  sb x[0-9]+, [0-9]+\(x[0-9]+\)$/ {
            offset = $3; sub(/\(.*/, "", offset); scatters[offset]++
        }
        within && $0 ~ /^  lbu x[0-9]+, [0-9]+\(x[0-9]+\)$/ {
            offset = $3; sub(/\(.*/, "", offset); gathers[offset]++
        }
        END {
            for (i = 0; i < expected; ++i)
                if (!scatters[i] || !gathers[i]) missing++
            if (!found || !done || !odd_frame || missing) {
                printf "RV64_ODD_FRAME_AGGREGATE=FAIL %s size=%d odd_frame=%d missing_offsets=%d\n", \
                    name, expected, odd_frame, missing > "/dev/stderr"
                exit 1
            }
        }
    ' "$work/packed.s"
done

awk '
    $0 == "call_after_one_four:" { within = 1; found = 1; next }
    within && index($0, ".size call_after_one_four,") == 1 { within = 0; done = 1 }
    within && $0 ~ /^  addi x[0-9]+, x2, 5$/ { first_odd_address++ }
    within && $0 ~ /^  addi x[0-9]+, x2, 6$/ { second_odd_address++ }
    within && $0 == "  call mix_after_one_four" { called++ }
    within && $0 ~ /^  sb x[0-9]+, [0-9]+\(x[0-9]+\)$/ {
        offset = $3; sub(/\(.*/, "", offset); scatter[offset]++
    }
    within && $0 ~ /^  lbu x[0-9]+, [0-9]+\(x[0-9]+\)$/ {
        offset = $3; sub(/\(.*/, "", offset); gather[offset]++
    }
    END {
        for (i = 0; i < 4; ++i)
            if (!scatter[i] || !gather[i]) missing++
        if (!found || !done || !first_odd_address || !second_odd_address ||
            !called || missing) {
            printf "RV64_ODD_FRAME_CALL=FAIL odd_addresses=%d,%d call=%d missing_offsets=%d\n", \
                first_odd_address, second_odd_address, called, missing > "/dev/stderr"
            exit 1
        }
    }
' "$work/packed.s"

echo 'RV64_ALIGN_SAFE_RECORD_LOAD=PASS packed/natural=2,4,8 nested_source_offset=1 odd_frame_abi=2,4,8 odd_call=4'
