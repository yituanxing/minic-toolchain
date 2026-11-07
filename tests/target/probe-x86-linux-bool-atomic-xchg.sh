#!/bin/sh
set -eu

minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

"$host_cc" -E -P "$source_root/tests/target/x86_64_linux_bool_atomic_xchg.c" \
    -o "$workdir/atom.i"
"$minic_cc" -S "$workdir/atom.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/atom.s"
"$host_cc" -c -x assembler "$workdir/atom.s" -o "$workdir/atom.o"
objdump -d "$workdir/atom.o" > "$workdir/atom.dump"
if ! grep -Eq 'xchg.*%al,\(%r10\)' "$workdir/atom.dump"; then
    printf 'FAIL: one-byte Linux bool xchg lost its hardware RMW\n' >&2
    exit 1
fi
"$host_cc" -no-pie "$source_root/tests/target/x86_64_linux_bool_atomic_xchg_driver.c" \
    "$workdir/atom.o" -o "$workdir/minic-run"
"$workdir/minic-run"

"$host_cc" -c "$source_root/tests/target/x86_64_linux_bool_atomic_xchg.c" \
    -o "$workdir/gnu.o"
"$host_cc" -no-pie "$source_root/tests/target/x86_64_linux_bool_atomic_xchg_driver.c" \
    "$workdir/gnu.o" -o "$workdir/gnu-run"
"$workdir/gnu-run"

"$host_cc" -E -P -DMINIC_BOOL_XCHG_NEGATIVE \
    "$source_root/tests/target/x86_64_linux_bool_atomic_xchg.c" \
    -o "$workdir/negative.i"
if "$minic_cc" -S "$workdir/negative.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE \
    -o "$workdir/negative.s" >/dev/null 2>&1; then
    printf 'FAIL: bool xchg without compiler memory clobber accepted\n' >&2
    exit 1
fi

printf 'X86_BOOL_ATOMIC_XCHG=PASS MiniC_GNU_AS=1 native_GNU_differential=2 strict_negative=1\n'
