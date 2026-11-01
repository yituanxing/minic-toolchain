#!/bin/sh
# MC-038: promoted/nested record designator positional continuation.
set -eu

: "${MINIC_CC:?set MINIC_CC to the compiler under test}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source_file="$root/tests/compiler/audit/mc038_anonymous_designator_runtime.c"
support_file="$root/tests/compiler/audit/mc038_anonymous_designator_runtime_support.c"
invalid_file="$root/tests/compiler/audit/mc038_anonymous_designator_overflow.c"

"$host_cc" -std=gnu11 -O0 -Wall -Wextra -Wno-missing-braces -Werror \
    "$source_file" "$support_file" -o "$work/gnu"
"$host_cc" -std=gnu11 -E -P "$source_file" -o "$work/source.i"
for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$MINIC_CC" --target="$target" -S "$work/source.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
done
"$host_cc" -std=gnu11 -O0 -Wall -Wextra -Werror -no-pie \
    "$work/x86_64-linux.s" "$support_file" -o "$work/minic"
"$work/gnu" > "$work/gnu.out"
"$work/minic" > "$work/minic.out"
cmp -s "$work/gnu.out" "$work/minic.out"
if "$host_cc" -std=gnu11 -pedantic-errors -fsyntax-only "$invalid_file" \
        > "$work/negative-gnu.out" 2> "$work/negative-gnu.err"; then
    echo 'MC-038: GNU unexpectedly accepted overflow initializer' >&2
    exit 1
fi
"$host_cc" -E -P -std=gnu11 "$invalid_file" -o "$work/negative.i"
if "$MINIC_CC" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.s" \
        > "$work/negative-minic.out" 2> "$work/negative-minic.err"; then
    echo 'MC-038: MiniC unexpectedly accepted overflow initializer' >&2
    exit 1
fi
grep -F 'too many positional record initializers' "$work/negative-minic.err" >/dev/null

rv64_qemu=skipped
if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1 &&
   command -v qemu-riscv64 >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -c "$work/riscv64-linux.s" -o "$work/riscv64.o"
    riscv64-linux-gnu-gcc -static -nostdlib -fno-pie -no-pie \
        -Wl,-e,_start -O0 -DMC038_FREESTANDING \
        "$work/riscv64.o" "$support_file" \
        "$root/tests/compiler/audit/mc038_rv64_entry.c" \
        -lgcc -o "$work/riscv64"
    qemu-riscv64 "$work/riscv64"
    rv64_qemu=passed
fi
echo "MC038_ANONYMOUS_DESIGNATOR_RUNTIME=PASS cases=13 overflow=reject five_target_S=5 GNU_MiniC_x86_native=0 rv64_qemu=$rv64_qemu"
