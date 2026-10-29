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
        within && $0 ~ /^  lbu t2, [0-9]+\(t0\)$/ { loads++ }
        within && $0 ~ /^  sb t2, [0-9]+\(t1\)$/ { stores++ }
        within && $0 == "  addi t0, t0, 1" { member_offset++ }
        END {
            if (!found || !done || loads != expected || stores != expected ||
                (name == "load_natural_field_packed" && member_offset == 0)) {
                printf "RV64_ALIGN_SAFE_RECORD_LOAD=FAIL %s bytes=%d loads=%d stores=%d member_offset=%d\n", \
                    name, expected, loads, stores, member_offset > "/dev/stderr"
                exit 1
            }
        }
    ' "$work/packed.s"
done

# The first packed by-value parameter occupies frame byte zero. The second
# record begins at byte one; incoming scalar words and return aggregates must
# therefore use the existing byte-scatter/gather paths, not unaligned sh/sw/sd
# or lhu/lwu/ld. The pre-fix emitted `sw a1,0(t3)` with t3=sp+1 and
# `lwu a0,0(t0)` with t0=sp+1 for the four-byte variant.
for item in two:2 four:4 eight:8; do
    suffix=${item%%:*}
    bytes=${item#*:}
    awk -v name="mix_after_one_$suffix" -v expected="$bytes" '
        $0 == name ":" { within = 1; found = 1; next }
        within && index($0, ".size " name ",") == 1 { within = 0; done = 1 }
        within && $0 ~ /^  sb t1, 0\(t3\)$/ { incoming_bytes++ }
        within && $0 ~ /^  lbu t6, [0-9]+\(t0\)$/ { return_bytes++ }
        END {
            if (!found || !done || incoming_bytes != expected || return_bytes != expected) {
                printf "RV64_ODD_FRAME_AGGREGATE=FAIL %s size=%d incoming=%d return=%d\n", \
                    name, expected, incoming_bytes, return_bytes > "/dev/stderr"
                exit 1
            }
        }
    ' "$work/packed.s"
done

awk '
    $0 == "call_after_one_four:" { within = 1; found = 1; next }
    within && index($0, ".size call_after_one_four,") == 1 { within = 0; done = 1 }
    within && $0 ~ /^  sb t1, 0\(t3\)$/ { scatter++ }
    within && $0 ~ /^  lbu t1, 0\(t3\)$/ { argument_gather++ }
    within && $0 ~ /^  lbu t6, [0-9]+\(t0\)$/ { result_gather++ }
    END {
        if (!found || !done || scatter != 8 || argument_gather != 4 || result_gather != 4) {
            printf "RV64_ODD_FRAME_CALL=FAIL scatter=%d argument_gather=%d result_gather=%d\n", \
                scatter, argument_gather, result_gather > "/dev/stderr"
            exit 1
        }
    }
' "$work/packed.s"

echo 'RV64_ALIGN_SAFE_RECORD_LOAD=PASS packed/natural=2,4,8 nested_source_offset=1 odd_frame_abi=2,4,8 odd_call=4'
