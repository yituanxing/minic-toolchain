#!/bin/sh
# MC-006: two distinct symbolic callers share one source inline CALL.
# Correctness is an ELF relocation property, not just clone count or exit 0.
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc binary}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/audit/mc006_shared_call_relocation.c"

jump_key_addends() {
    readelf -rW "$1" | awk '
        /^Relocation section .*\.rela__jump_table/ { in_table = 1; next }
        /^Relocation section / { in_table = 0 }
        in_table && /R_X86_64_PC64/ { print $NF }
    ' | sort -u
}

for order in forward reverse; do
    if test "$order" = reverse; then
        set -- -DMC006_REVERSE
    else
        set --
    fi
    "$host_cc" -std=gnu11 -O2 -Wall -Wextra -Werror -fno-pie \
        "$@" -c "$source" -o "$work/$order.gnu.o"
    "$host_cc" -std=gnu11 -E -P "$@" "$source" -o "$work/$order.i"
    "$MINIC_CC" --target=x86_64-linux -S "$work/$order.i" \
        -o "$work/$order.minic.s" 2> "$work/$order.minic.log"
    "$host_cc" -c -x assembler "$work/$order.minic.s" \
        -o "$work/$order.minic.o"

    for producer in gnu minic; do
        jump_key_addends "$work/$order.$producer.o" > "$work/$order.$producer.keys"
        # Both keys[0]+2 and keys[1]+2 (two 8-byte static key records)
        # must survive independently in GNU __jump_table metadata.
        for key_addend in 2 a; do
            if ! grep -Fxq "$key_addend" "$work/$order.$producer.keys"; then
                echo "MC006_FAIL order=$order producer=$producer missing=$key_addend" >&2
                sed -n '1,10p' "$work/$order.$producer.keys" >&2
                exit 1
            fi
        done
        if test "$(wc -l < "$work/$order.$producer.keys")" -ne 2; then
            echo "MC006_FAIL order=$order producer=$producer unexpected-key-count" >&2
            exit 1
        fi
        "$host_cc" -no-pie "$work/$order.$producer.o" \
            -o "$work/$order.$producer.elf"
        "$work/$order.$producer.elf"
    done
done

echo 'MC006_SHARED_CALL_RELOCATIONS=PASS orders=2 GNU_MiniC_x86_ELF=4 native=4 keys=2_each'
