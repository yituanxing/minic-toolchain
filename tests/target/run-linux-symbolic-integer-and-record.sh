#!/bin/sh
# GNU Linux kernel static integer relocations and record statement expressions.
# Link-time-only external symbols intentionally remain unresolved: compile
# those sections separately from the native aggregate runtime test.
set -eu

minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

"$host_cc" -E -P "$source_root/tests/target/linux_symbolic_integer_and_record.c" \
    -o "$workdir/linux_symbolic_integer_and_record.i"

for target in x86_64-linux aarch64-linux loongarch64-linux riscv64-linux riscv32-linux
do
    "$minic_cc" -S "$workdir/linux_symbolic_integer_and_record.i" \
        --target="$target" \
        -o "$workdir/$target.s"
    if ! grep -Fq 'kernel_link_base' "$workdir/$target.s"; then
        printf 'FAIL: symbolic integer relocation disappeared on %s\n' "$target" >&2
        exit 1
    fi
done

"$host_cc" -c -x assembler "$workdir/x86_64-linux.s" -o "$workdir/linux.o"
readelf -r "$workdir/linux.o" > "$workdir/relocations.txt"
if [ "$(grep -c 'kernel_link_base' "$workdir/relocations.txt")" -lt 65 ]; then
    printf 'FAIL: 64 array entries plus scalar link-time relocation not preserved\n' >&2
    exit 1
fi
if ! grep -Fq 'kernel_link_object' "$workdir/relocations.txt"; then
    printf 'FAIL: signed-byte-addend symbolic integer relocation missing\n' >&2
    exit 1
fi

# The record-valued GNU statement expression must run identically to GCC;
# external linker-script symbols are supplied by the fixture's own harness.
"$host_cc" -c -x assembler "$workdir/x86_64-linux.s" -o "$workdir/runtime.o"
"$host_cc" -x c -c -o "$workdir/linker_symbols.o" - <<'EOF'
char kernel_link_base[64];
int kernel_link_object;
EOF
"$host_cc" -no-pie "$workdir/runtime.o" "$workdir/linker_symbols.o" \
    -o "$workdir/runtime"
"$workdir/runtime"

printf 'PASS: five-target pointer-sized integer static relocations, 64-slot GNU designator and x86 aggregate statement runtime\n'
