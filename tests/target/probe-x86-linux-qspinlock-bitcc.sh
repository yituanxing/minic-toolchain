#!/bin/sh
set -eu

minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

"$host_cc" -E -P "$source_root/tests/target/x86_64_linux_qspinlock_bitcc.c" \
    -o "$workdir/bitcc.i"
"$minic_cc" -S "$workdir/bitcc.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/bitcc.s"
"$host_cc" -c -x assembler "$workdir/bitcc.s" -o "$workdir/bitcc.o"
objdump -drwC "$workdir/bitcc.o" > "$workdir/bitcc.dump"
readelf -rW "$workdir/bitcc.o" > "$workdir/bitcc.reloc"
if ! grep -Eq 'lock btsl? \$0x8,\(%r10\)' "$workdir/bitcc.dump" ||
   ! grep -Eq 'setb[[:space:]]+%r9b' "$workdir/bitcc.dump" ||
   ! grep -Fq "Relocation section '.rela.smp_locks'" "$workdir/bitcc.reloc" ||
   ! grep -Fq 'R_X86_64_PC32' "$workdir/bitcc.reloc"; then
    printf 'FAIL: qspinlock lost LOCK BTSL/CF/PC32 SMP patch relocation\n' >&2
    exit 1
fi
# The addend is the offset of the locked instruction itself in .text.
# A record targeting the subsequent SETC would be a silent Linux SMP bug.
target_hex=$(awk '
    /Relocation section '\''\.rela\.smp_locks'\''/ { in_smp = 1; next }
    /Relocation section/ { in_smp = 0 }
    in_smp && /R_X86_64_PC32/ {
        for (i = 1; i < NF; ++i)
            if ($i == "+") print $(i + 1)
    }
' "$workdir/bitcc.reloc")
bts_hex=$(sed -n '/lock btsl/ s/^[[:space:]]*\([0123456789abcdef][0123456789abcdef]*\):.*/\1/p' \
    "$workdir/bitcc.dump")
if [ -z "$target_hex" ] || [ -z "$bts_hex" ] ||
   [ "$(printf '%d' "0x$target_hex")" -ne "$(printf '%d' "0x$bts_hex")" ]; then
    printf 'FAIL: .smp_locks PC32 points past the locked BTS instruction\n' >&2
    exit 1
fi
"$host_cc" -no-pie \
    "$source_root/tests/target/x86_64_linux_qspinlock_bitcc_driver.c" \
    "$workdir/bitcc.o" -o "$workdir/minic-run"
"$workdir/minic-run"
"$host_cc" -c "$source_root/tests/target/x86_64_linux_qspinlock_bitcc.c" \
    -o "$workdir/gnu.o"
"$host_cc" -no-pie \
    "$source_root/tests/target/x86_64_linux_qspinlock_bitcc_driver.c" \
    "$workdir/gnu.o" -o "$workdir/gnu-run"
"$workdir/gnu-run"

for mode in 1 2 3 4; do
    "$host_cc" -E -P -DMINIC_QSPINLOCK_BITCC_NEGATIVE="$mode" \
        "$source_root/tests/target/x86_64_linux_qspinlock_bitcc.c" \
        -o "$workdir/negative-$mode.i"
    if "$minic_cc" -S "$workdir/negative-$mode.i" --target=x86_64-linux \
        -fshort-wchar -funsigned-char -fno-PIE \
        -o "$workdir/negative-$mode.s" >/dev/null 2>&1; then
        printf 'FAIL: qspinlock invalid source variant %s accepted\n' "$mode" >&2
        exit 1
    fi
done

printf 'X86_QSPINLOCK_LOCK_BITCC=PASS GNU_AS=1 SMP_PC32=1 CF=1 native_GNU_differential=2 strict_negative=4\n'
