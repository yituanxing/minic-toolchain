#!/usr/bin/env bash
# Both Linux TESTB flag-output variants must survive typed M1 and native ELF.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
fixture=${BASH_SOURCE[0]%/*}/x86_linux_testcc.c
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

gcc -std=gnu11 -E -P "$fixture" -o "$work/mini.i"
"$mini" --target=x86_64-linux -S "$work/mini.i" -o "$work/mini.s"
as --64 -o "$work/mini.o" "$work/mini.s"
gcc -no-pie -o "$work/mini.run" "$work/mini.o"
"$work/mini.run"
gcc -std=gnu11 -O2 -o "$work/gcc.run" "$fixture"
"$work/gcc.run"

test "$(grep -cF 'testb $' "$work/mini.s")" -eq 3
grep -qF 'setnz %r9b' "$work/mini.s"
grep -qF 'sete %r9b' "$work/mini.s"

sed '0,/=@ccnz/s//=@ccz/' "$fixture" > "$work/bad-flag.c"
sed '0,/"m" (/s//"r" (/' "$fixture" > "$work/bad-memory.c"
sed '0,/"i" (8)/s//"i" (256)/' "$fixture" > "$work/bad-immediate.c"
for negative in bad-flag bad-memory bad-immediate; do
    gcc -std=gnu11 -E -P "$work/$negative.c" -o "$work/$negative.i"
    if "$mini" --target=x86_64-linux -S "$work/$negative.i" \
            -o "$work/$negative.s" >"$work/$negative.out" 2>"$work/$negative.err"; then
        echo "FAIL: $negative TESTCC was admitted" >&2
        exit 1
    fi
done
echo 'MINIC_X86_TESTCC=PASS runtime_MiniC_GNUAS=1 runtime_GCC=1 testb=3 negatives=3'
