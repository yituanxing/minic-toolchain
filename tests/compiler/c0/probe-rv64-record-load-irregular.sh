#!/bin/sh
# Validate every byte of irregular RV64 record loads; a wider frame write
# after loading only byte zero used to silently corrupt 3/5/6/7-byte records.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

${CC:-cc} -E -P -x c "$root/tests/compiler/c0/rv64_record_load_irregular.c" \
    -o "$work/record.i"
"$MINIC_CC" --target=riscv64-linux -S "$work/record.i" -o "$work/record.s"

for item in three:3 five:5 six:6 seven:7 nine:9 fifteen:15; do
    func=${item%%:*}
    bytes=${item#*:}
    awk -v name="load_$func" -v expected="$bytes" '
        $0 == name ":" { within = 1; next }
        within && index($0, ".size " name ",") == 1 { within = 0; done = 1 }
        within && $0 ~ /^  lbu t2, [0-9]+\(t0\)$/ { loads++ }
        within && $0 ~ /^  sb t2, [0-9]+\(t1\)$/ { stores++ }
        END {
            if (!done || loads != expected || stores != expected) {
                printf "RV64_RECORD_LOAD=FAIL %s bytes=%d loads=%d stores=%d\n", \
                    name, expected, loads, stores > "/dev/stderr"
                exit 1
            }
        }
    ' "$work/record.s"
done
echo 'RV64_RECORD_LOAD=PASS byte_exact_sizes=3,5,6,7,9,15'
