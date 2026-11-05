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
        # The old FILE* emitter fixed t0/t1/t2. MachineIR allocates physical
        # registers independently; check actual byte-for-byte copy pairs,
        # including their offsets, without depending on register spelling.
        within && $0 ~ /^  lbu [[:alnum:]_]+, [0-9]+\([[:alnum:]_]+\)$/ {
            load_reg = $0
            sub(/^  lbu /, "", load_reg)
            sub(/,.*/, "", load_reg)
            load_offset = $0
            sub(/^  lbu [^,]+, /, "", load_offset)
            sub(/\(.*/, "", load_offset)
            load_offset += 0
            next
        }
        within && $0 ~ /^  sb [[:alnum:]_]+, [0-9]+\([[:alnum:]_]+\)$/ {
            store_reg = $0
            sub(/^  sb /, "", store_reg)
            sub(/,.*/, "", store_reg)
            store_offset = $0
            sub(/^  sb [^,]+, /, "", store_offset)
            sub(/\(.*/, "", store_offset)
            store_offset += 0
            stores++
            if (load_reg != "" && load_reg == store_reg &&
                load_offset == store_offset) {
                loads++
                copied[store_offset]++
            }
            load_reg = ""
            next
        }
        within { load_reg = "" }
        END {
            complete = done && loads == expected && stores == expected
            for (offset = 0; offset < expected; offset++)
                if (copied[offset] != 1)
                    complete = 0
            if (!complete) {
                printf "RV64_RECORD_LOAD=FAIL %s bytes=%d loads=%d stores=%d\n", \
                    name, expected, loads, stores > "/dev/stderr"
                exit 1
            }
        }
    ' "$work/record.s"
done
echo 'RV64_RECORD_LOAD=PASS byte_exact_sizes=3,5,6,7,9,15'
