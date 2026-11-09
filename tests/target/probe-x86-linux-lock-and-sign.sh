#!/bin/sh
set -eu

minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

"$host_cc" -E -P "$source_root/tests/target/x86_64_linux_lock_and_sign.c" \
    -o "$workdir/lock.i"
"$minic_cc" -S "$workdir/lock.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/lock.s"
"$host_cc" -c -x assembler "$workdir/lock.s" -o "$workdir/lock.o"
objdump -drwC "$workdir/lock.o" > "$workdir/lock.dump"
readelf -rW "$workdir/lock.o" > "$workdir/lock.reloc"
if ! grep -Eq 'lock and[[:space:]]+%r11b,\(%r10\)' "$workdir/lock.dump" ||
   ! grep -Eq 'sets[[:space:]]+%r9b' "$workdir/lock.dump" ||
   ! grep -Fq "Relocation section '.rela.smp_locks'" "$workdir/lock.reloc" ||
   ! grep -Fq 'R_X86_64_PC32' "$workdir/lock.reloc"; then
    printf 'FAIL: locked AND lost hardware RMW / SF / SMP patch relocation\n' >&2
    exit 1
fi
# The patch-table PC32 addend must name LOCK AND itself, not the SETS
# instruction after it; otherwise x86 Linux SMP alternatives patch the
# wrong byte range even though GNU AS accepts the object.
target_hex=$(awk '
    /Relocation section '\''.rela.smp_locks'\''/ { in_smp = 1; next }
    /Relocation section/ { in_smp = 0 }
    in_smp && /R_X86_64_PC32/ {
        for (i = 1; i < NF; ++i)
            if ($i == "+") print $(i + 1)
    }
' "$workdir/lock.reloc")
and_hex=$(sed -n '/lock and/ s/^[[:space:]]*\([0123456789abcdef][0123456789abcdef]*\):.*/\1/p' \
    "$workdir/lock.dump")
if [ -z "$target_hex" ] || [ -z "$and_hex" ] ||
   [ "$(printf '%d' "0x$target_hex")" -ne "$(printf '%d' "0x$and_hex")" ]; then
    printf 'FAIL: .smp_locks PC32 does not target LOCK AND opcode\n' >&2
    exit 1
fi
"$host_cc" -no-pie "$source_root/tests/target/x86_64_linux_lock_and_sign_driver.c" \
    "$workdir/lock.o" -o "$workdir/minic-run"
"$workdir/minic-run"
"$host_cc" -c "$source_root/tests/target/x86_64_linux_lock_and_sign.c" \
    -o "$workdir/gnu.o"
"$host_cc" -no-pie "$source_root/tests/target/x86_64_linux_lock_and_sign_driver.c" \
    "$workdir/gnu.o" -o "$workdir/gnu-run"
"$workdir/gnu-run"

for variant in 1 2 3 4 5; do
    "$host_cc" -E -P -DMINIC_LOCK_AND_SIGN_NEGATIVE="$variant" \
        "$source_root/tests/target/x86_64_linux_lock_and_sign.c" \
        -o "$workdir/negative-$variant.i"
    if "$minic_cc" -S "$workdir/negative-$variant.i" --target=x86_64-linux \
        -fshort-wchar -funsigned-char -fno-PIE \
        -o "$workdir/negative-$variant.s" >/dev/null 2>&1; then
        printf 'FAIL: invalid locked-AND form %s accepted\n' "$variant" >&2
        exit 1
    fi
done

printf 'X86_LOCK_AND_SIGN=PASS GNU_AS=1 SMP_PC32=1 SF=1 native_GNU_differential=4096 strict_negative=5\n'
