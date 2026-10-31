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
    /^  pcaddi \$t6, 2$/ {
        if (name == "write_packed_wide") wide_continuations++
    }
    /^  b \.Lmini_loong_f[0-9]+_unaligned_store8$/ {
        if (name == "write_packed_wide") {
            target = $0
            sub(/^  b /, "", target)
            if (shared_store_target != "" && shared_store_target != target)
                distinct_store_targets++
            shared_store_target = target
            outlined_wide_stores++
        }
    }
    /^\.Lmini_loong_f[0-9]+_unaligned_store8:$/ {
        if ($0 == shared_store_target ":") {
            shared_store_helper++
            in_store_helper = 1
        }
    }
    in_store_helper && /^  li.d \$t3, 8$/ { helper_count8++ }
    in_store_helper && /^  st.b \$t0, \$t1, 0$/ { helper_byte_store++ }
    in_store_helper && /^  srli.d \$t0, \$t0, 8$/ { helper_shift++ }
    in_store_helper && /^  addi.d \$t1, \$t1, 1$/ { helper_address_step++ }
    in_store_helper && /^  addi.d \$t3, \$t3, -1$/ { helper_counter_step++ }
    in_store_helper && /^  bnez \$t3, 1b$/ { helper_loop++ }
    in_store_helper && /^  jirl \$zero, \$t6, 0$/ {
        helper_return++
        in_store_helper = 0
    }
    /^  slli.w \$t0, \$t0, 0$/ { narrow_ext++ }
    END {
        if (guard4 < 2 || guard8 < 1 || wide_load_guard != 2 ||
            wide_store_guard != 2 || bad_static_proof ||
            byte_load < 2 || byte_store < 1 || narrow_ext < 1 ||
            outlined_wide_stores != 2 || wide_continuations != 2 ||
            distinct_store_targets || shared_store_helper != 1 ||
            helper_count8 != 1 || helper_byte_store != 1 ||
            helper_shift != 1 || helper_address_step != 1 ||
            helper_counter_step != 1 || helper_loop != 1 ||
            helper_return != 1) {
            printf "LOONGARCH_PACKED_ALIGNMENT=FAIL packed_guard4=%d guard8=%d wide_load=%d wide_store=%d static_guard=%d byte_load=%d byte_store=%d sign_ext=%d outlined8=%d continuations=%d helper=%d count8=%d byte=%d shift=%d step=%d dec=%d loop=%d return=%d\n", guard4, guard8, wide_load_guard, wide_store_guard, bad_static_proof, byte_load, byte_store, narrow_ext, outlined_wide_stores, wide_continuations, shared_store_helper, helper_count8, helper_byte_store, helper_shift, helper_address_step, helper_counter_step, helper_loop, helper_return > "/dev/stderr"
            exit 1
        }
    }
' "$work/source.s"
# pcaddi uses a signed instruction-count displacement, not a symbol relocation:
# the helper branch must occupy exactly one instruction before continuation 2:.
# This also keeps the emitted form acceptable to Clang's integrated assembler.
awk '
    /^  pcaddi \$t6, 2$/ {
        seen++
        if (getline branch != 1 ||
            branch !~ /^  b \.Lmini_loong_f[0-9]+_unaligned_(load4|load8|store8)$/ ||
            getline continuation != 1 || continuation != "2:") bad++
    }
    END { if (seen < 2 || bad) exit 1 }
' "$work/source.s" || {
    echo 'LOONGARCH_PACKED_ALIGNMENT=FAIL pcaddi branch/continuation PC+8 layout' >&2
    exit 1
}
echo 'LOONGARCH_PACKED_ALIGNMENT=PASS -S packed scalar/pointer/wide guards=1 static_global_wide_fastpath=1 outlined8_byte_fallback=2 shared_helper=1'
