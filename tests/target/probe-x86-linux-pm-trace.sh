#!/usr/bin/env bash
# Genuine drivers/base/power/main.c PM_TRACE ELF record, hardware output
# register and relocation semantics.  No guest or privileged instruction.
set -Eeuo pipefail
minic=${1:-build/debug/bin/minic-cc}
test -x "$minic"
root=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$root/build/tests"
tmp=$(mktemp -d "$root/build/tests/x86-pm-trace.XXXXXX")
trap 'rm -rf -- "$tmp"' EXIT
source_file="$root/tests/target/x86_64_linux_pm_trace.c"
cc -E -P -x c "$source_file" -o "$tmp/pm_trace.i"
"$minic" --target=x86_64-linux -fno-PIE -S "$tmp/pm_trace.i" -o "$tmp/minic.s"
as --64 "$tmp/minic.s" -o "$tmp/minic.o"
cc -O2 -fno-PIE -fno-pic -mcmodel=kernel -c "$source_file" -o "$tmp/gnu.o"

grep -Fq 'movq $1f, %r10' "$tmp/minic.s"
grep -Fq 'movq %r10, (%r11)' "$tmp/minic.s"
grep -Fq '.section .tracedata,"a"' "$tmp/minic.s"
for binary in "$tmp/minic.o" "$tmp/gnu.o"; do
    readelf -rW "$binary" | grep -Eq 'R_X86_64_32S[[:space:]]+.*\.tracedata'
    readelf -rW "$binary" | grep -Eq 'R_X86_64_64[[:space:]]+.*\.rodata'
    readelf -SW "$binary" | grep -Eq '\.tracedata[[:space:]]+PROGBITS'
done
objcopy --dump-section .tracedata="$tmp/minic.trace" "$tmp/minic.o"
objcopy --dump-section .tracedata="$tmp/gnu.trace" "$tmp/gnu.o"
test "$(stat -c %s "$tmp/minic.trace")" -eq 10
cmp "$tmp/minic.trace" "$tmp/gnu.trace"

for negative in TEMPLATE LINE_CONSTRAINT LINE_RANGE RUNTIME_FILENAME MEMORY_CLOBBER; do
    cc -E -P -x c -DMINIC_PM_TRACE_NEG_"$negative" \
        "$source_file" -o "$tmp/bad-$negative.i"
    if "$minic" --target=x86_64-linux -S "$tmp/bad-$negative.i" \
        -o "$tmp/bad-$negative.s" >/dev/null 2>&1; then
        echo "FAIL nonphysical PM_TRACE source variant admitted: $negative" >&2
        exit 1
    fi
done

if [[ -n "${MINIC_X86_PM_TRACE_ORIGINAL_I:-}" ]]; then
    test -s "$MINIC_X86_PM_TRACE_ORIGINAL_I"
    "$minic" --target=x86_64-linux -fshort-wchar -funsigned-char \
        -fno-PIE -S "$MINIC_X86_PM_TRACE_ORIGINAL_I" \
        -o "$tmp/original.s"
    as --64 "$tmp/original.s" -o "$tmp/original.o"
    objcopy --dump-section .tracedata="$tmp/original.trace" "$tmp/original.o"
    test "$(stat -c %s "$tmp/original.trace")" -eq 120
    test "$(readelf -rW "$tmp/original.o" |
        grep -Ec 'R_X86_64_32S[[:space:]]+.*\.tracedata')" -eq 12
    test "$(readelf -rW "$tmp/original.o" |
        grep -Ec 'R_X86_64_64[[:space:]]+.*\.rodata')" -ge 12
    cc -x c -fpreprocessed -std=gnu11 -O2 -fno-PIE -fno-pic \
        -mcmodel=kernel -fshort-wchar -funsigned-char -mno-sse -mno-red-zone \
        -Wno-attributes -c "$MINIC_X86_PM_TRACE_ORIGINAL_I" \
        -o "$tmp/original.gnu.o"
    objcopy --dump-section .tracedata="$tmp/original.gnu.trace" \
        "$tmp/original.gnu.o"
    test "$(stat -c %s "$tmp/original.gnu.trace")" -eq 120
    test "$(readelf -rW "$tmp/original.gnu.o" |
        grep -Ec 'R_X86_64_32S[[:space:]]+.*\.tracedata')" -eq 12
    test "$(readelf -rW "$tmp/original.gnu.o" |
        grep -Ec 'R_X86_64_64[[:space:]]+.*\.rodata')" -ge 12
    # Kbuild GNU -O2 and MiniC may choose different function order, but
    # every 10-byte ELF line/address record must be present in both outputs.
    diff -u <(od -An -v -tx1 -w10 "$tmp/original.gnu.trace" | sort) \
        <(od -An -v -tx1 -w10 "$tmp/original.trace" | sort)
    echo 'MINIC_X86_PM_TRACE_ORIGINAL_GNU_E=PASS owner=drivers/base/power/main records=12 multiset=exact'
fi
echo 'MINIC_X86_PM_TRACE=PASS GNU_AS=1 GNU_ELF_ABS32S=1 record_ABS64=1 data_equal=1 negatives=5'
