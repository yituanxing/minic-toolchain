#!/bin/sh
set -eu

minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

"$host_cc" -E -P "$source_root/tests/target/x86_64_linux_ipv6_checksum.c" \
    -o "$workdir/ipv6.i"
"$minic_cc" -S "$workdir/ipv6.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/ipv6.s"
"$host_cc" -c -x assembler "$workdir/ipv6.s" -o "$workdir/ipv6.o"
objdump -drwC "$workdir/ipv6.o" > "$workdir/ipv6.dump"
if ! grep -Eq 'add[[:space:]]+\(%r10\),%rax' "$workdir/ipv6.dump" ||
   ! grep -Eq 'adc[[:space:]]+0x8\(%r10\),%rax' "$workdir/ipv6.dump" ||
   ! grep -Eq 'adc[[:space:]]+\(%r11\),%rax' "$workdir/ipv6.dump" ||
   ! grep -Eq 'adc[[:space:]]+0x8\(%r11\),%rax' "$workdir/ipv6.dump" ||
   ! grep -Eq 'adc[[:space:]]+\$0x0,%rax' "$workdir/ipv6.dump"; then
    printf 'FAIL: IPv6 checksum lost authentic memory-based CF chain\n' >&2
    exit 1
fi
"$host_cc" -no-pie "$source_root/tests/target/x86_64_linux_ipv6_checksum_driver.c" \
    "$workdir/ipv6.o" -o "$workdir/minic-run"
"$workdir/minic-run"

"$host_cc" -c "$source_root/tests/target/x86_64_linux_ipv6_checksum.c" \
    -o "$workdir/gnu.o"
"$host_cc" -no-pie "$source_root/tests/target/x86_64_linux_ipv6_checksum_driver.c" \
    "$workdir/gnu.o" -o "$workdir/gnu-run"
"$workdir/gnu-run"

for negative in 1 2 3 4; do
    "$host_cc" -E -P -DMINIC_IPV6_CSUM_NEGATIVE="$negative" \
        "$source_root/tests/target/x86_64_linux_ipv6_checksum.c" \
        -o "$workdir/negative-$negative.i"
    if "$minic_cc" -S "$workdir/negative-$negative.i" \
        --target=x86_64-linux -fshort-wchar -funsigned-char -fno-PIE \
        -o "$workdir/negative-$negative.s" >/dev/null 2>&1; then
        printf 'FAIL: invalid IPv6 checksum source variant %s accepted\n' \
            "$negative" >&2
        exit 1
    fi
done

printf 'X86_IPV6_CHECKSUM=PASS GNU_AS=1 CF_CHAIN=1 native_GNU_differential=2 strict_negative=4\n'
