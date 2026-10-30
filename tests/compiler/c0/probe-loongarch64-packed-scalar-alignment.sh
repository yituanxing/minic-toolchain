#!/bin/sh
# Core memory pointer provenance is not an alignment proof. LoongArch must
# guard native 16/32/64-bit memory ops and byte-gather/scatter the unaligned
# branch, including packed bit-fields, scalar members and pointer members.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

${CC:-cc} -E -P -x c \
    "$root/tests/compiler/c0/loongarch64_packed_bitfield_alignment.c" \
    -o "$work/source.i"
"$MINIC_CC" --target=loongarch64-linux -S "$work/source.i" \
    -o "$work/source.s"

for name in read_packed_high write_packed_high read_packed_tail \
            read_packed_scalar write_packed_scalar read_packed_pointer \
            read_natural_scalar read_natural_pointer \
            read_packed_wide write_packed_wide read_static_wide; do
    grep -Eq "^${name}:$" "$work/source.s" || {
        echo "LOONGARCH_PACKED_ALIGNMENT=FAIL missing $name" >&2; exit 1;
    }
done

awk '
    /^[a-z_]+:$/ { name = substr($0, 1, length($0)-1) }
    /^  andi \$t3, \$t1, 3$/ {
        if (name == "read_packed_scalar" || name == "write_packed_scalar") guard4++
    }
    /^  andi \$t3, \$t1, 7$/ {
        if (name == "read_packed_pointer") guard8++
        if (name == "read_packed_wide") wide_load_guard++
        if (name == "write_packed_wide") wide_store_guard++
        if (name == "read_static_wide") bad_static_proof++
    }
    /^  ld.bu \$t2, \$t1, 0$/ { byte_load++ }
    /^  st.b \$t0, \$t1, 0$/ { byte_store++ }
    /^\.Lloong_store_bytes_[0-9]+:$/ {
        if (name == "write_packed_wide") counted_wide_store++
    }
    /^  li.d \$t3, 8$/ {
        if (name == "write_packed_wide") byte_loop_count++
    }
    /^  slli.w \$t0, \$t0, 0$/ { narrow_ext++ }
    END {
        if (guard4 < 2 || guard8 < 1 || wide_load_guard != 2 ||
            wide_store_guard != 2 || bad_static_proof ||
            byte_load < 2 || byte_store < 1 || narrow_ext < 1 ||
            counted_wide_store != 2 || byte_loop_count != 2) {
            printf "LOONGARCH_PACKED_ALIGNMENT=FAIL packed_guard4=%d guard8=%d wide_load=%d wide_store=%d static_guard=%d byte_load=%d byte_store=%d sign_ext=%d counted8=%d\n", guard4, guard8, wide_load_guard, wide_store_guard, bad_static_proof, byte_load, byte_store, narrow_ext, counted_wide_store > "/dev/stderr"
            exit 1
        }
    }
' "$work/source.s"
echo 'LOONGARCH_PACKED_ALIGNMENT=PASS -S packed scalar/pointer/wide guards=1 static_global_wide_fastpath=1 counted8_byte_fallback=2'
