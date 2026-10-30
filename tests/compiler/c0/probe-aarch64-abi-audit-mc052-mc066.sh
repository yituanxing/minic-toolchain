#!/bin/sh
# AArch64 native ABI correctness: packed uaccess C writeback and FP pointers
# in large indirect records. GNU AS/ELF attestation when cross tools exist.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

src="$root/tests/compiler/audit/mc052_aarch64_uaccess_narrow_output.c"
"$host" -E -P -std=gnu11 "$src" -o "$work/uaccess.i"
"$mini" --target=aarch64-linux -S "$work/uaccess.i" -o "$work/uaccess.s"
for spec in 'read_u8:ldtrb w10:strb w10' \
            'read_u16:ldtrh w10:strh w10' \
            'read_u32:ldtr w10:str w10' \
            'read_u64:ldtr x10:str x10' \
            'read_pointer:ldtr x10:str x10'; do
    function_name=${spec%%:*}
    pair=${spec#*:}
    machine_load=${pair%%:*}
    machine_store=${pair#*:}
    awk -v function_name="$function_name" -v machine_load="$machine_load" \
        -v machine_store="$machine_store" '
        $0 == function_name ":" { active = 1; next }
        active && $0 ~ "^\\.size " function_name "," { active = 0 }
        active && index($0, "1: " machine_load) { loads++ }
        active && index($0, "  " machine_store ", [x12]") { stores++ }
        END { exit !(loads == 1 && stores == 1) }
    ' "$work/uaccess.s"
done
"$host" -E -P -std=gnu11 -DMC052_OVERSIZE_NEGATIVE "$src" \
    -o "$work/uaccess-oversized.i"
if "$mini" --target=aarch64-linux -S "$work/uaccess-oversized.i" \
        -o "$work/uaccess-oversized.s" >/dev/null 2>&1; then
    echo 'AARCH64_UACCESS=FAIL accepted 16-byte scalar in one GPR' >&2
    exit 1
fi

src="$root/tests/compiler/audit/mc066_aarch64_fp_pointer_aggregate.c"
for variant in default MC066_INTEGER_PTR MC066_POINTER_ARRAY; do
    if [ "$variant" = default ]; then
        "$host" -E -P -std=gnu11 "$src" -o "$work/pointer.i"
    else
        "$host" -E -P -std=gnu11 -D"$variant" "$src" -o "$work/pointer.i"
    fi
    "$mini" --target=aarch64-linux -S "$work/pointer.i" \
        -o "$work/pointer-$variant.s"
done
"$host" -E -P -std=gnu11 -DMC066_FLOAT_VALUES "$src" \
    -o "$work/fp-values.i"
if "$mini" --target=aarch64-linux -S "$work/fp-values.i" \
        -o "$work/fp-values.s" >/dev/null 2>&1; then
    echo 'AARCH64_RECORD=FAIL accepted unimplemented 4-double HFA' >&2
    exit 1
fi

gnu_elf=SKIP
gnu_as=${AARCH64_GNU_AS:-aarch64-linux-gnu-as}
gnu_readelf=${AARCH64_GNU_READELF:-aarch64-linux-gnu-readelf}
gnu_objcopy=${AARCH64_GNU_OBJCOPY:-aarch64-linux-gnu-objcopy}
gnu_cc=${AARCH64_GNU_CC:-aarch64-linux-gnu-gcc}
cross_qemu=${AARCH64_QEMU:-qemu-aarch64}
if command -v "$gnu_as" >/dev/null 2>&1 &&
   command -v "$gnu_readelf" >/dev/null 2>&1 &&
   command -v "$gnu_objcopy" >/dev/null 2>&1; then
    "$gnu_as" "$work/uaccess.s" -o "$work/uaccess.o"
    "$gnu_readelf" -rW "$work/uaccess.o" >"$work/relocations"
    "$gnu_objcopy" --dump-section "__ex_table=$work/ex_table.bin" "$work/uaccess.o"
    test "$(wc -c <"$work/ex_table.bin")" -eq 60
    relocations=$(grep -c 'R_AARCH64_PREL32' "$work/relocations" || true)
    test "$relocations" -eq 10
    gnu_elf=PASS
fi
gnu_qemu=SKIP
if [ "$gnu_elf" = PASS ] && command -v "$gnu_cc" >/dev/null 2>&1 &&
   command -v "$cross_qemu" >/dev/null 2>&1; then
    "$gnu_cc" -no-pie "$work/uaccess.o" -o "$work/uaccess-minic.elf"
    "$gnu_cc" -O2 -fno-pie -no-pie \
        "$root/tests/compiler/audit/mc052_aarch64_uaccess_narrow_output.c" \
        -o "$work/uaccess-gnu.elf"
    qemu_sysroot=${AARCH64_QEMU_SYSROOT:-/usr/aarch64-linux-gnu}
    "$cross_qemu" -L "$qemu_sysroot" "$work/uaccess-gnu.elf"
    "$cross_qemu" -L "$qemu_sysroot" "$work/uaccess-minic.elf"
    gnu_qemu=PASS
fi
echo "AARCH64_ABI_AUDIT=PASS uaccess_widths=1,2,4,8 pointer_output=1 negative_u128=1 fp_pointer_record=3 HFA_reject=1 GNU_AS_ELF=$gnu_elf GNU_QEMU=$gnu_qemu"
