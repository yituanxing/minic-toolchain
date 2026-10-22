#!/usr/bin/env bash
set -Eeuo pipefail
if [[ $# != 1 ]]; then echo "usage: $0 <minic-cc>" >&2; exit 64; fi
compiler=$(realpath "$1")
scratch=${RUNNER_TEMP:-/tmp}/minic-x86-record-abi
mkdir -p "$scratch"
cat > "$scratch/abi.c" <<'SOURCE'
#include <stdlib.h>
#include <stdio.h>
struct large { unsigned long a, b, c; unsigned char tail[5]; };
static unsigned long receive_by_value(struct large x, long bonus)
{
    return x.a + x.b * 3UL + x.c * 5UL
        + (unsigned long)x.tail[0] + (unsigned long)x.tail[4]
        + (unsigned long)bonus;
}
int main(void)
{
    struct large x;
    div_t pair = div(103, 9);
    x.a = 11; x.b = 13; x.c = 17;
    x.tail[0] = 2; x.tail[1] = 3; x.tail[2] = 4;
    x.tail[3] = 5; x.tail[4] = 7;
    printf("sysv-record %d:%d %lu\\n", pair.quot, pair.rem,
           receive_by_value(x, 19));
    return (pair.quot != 11 || pair.rem != 4
            || receive_by_value(x, 19) != 163UL);
}
SOURCE
cc -std=c11 -O0 "$scratch/abi.c" -o "$scratch/gnu"
cc -E -P -x c "$scratch/abi.c" -o "$scratch/abi.i"
"$compiler" --target=x86_64-linux -S "$scratch/abi.i" -o "$scratch/abi.s"
cc -x assembler -c "$scratch/abi.s" -o "$scratch/abi.o"
cc -no-pie "$scratch/abi.o" -o "$scratch/minic"
"$scratch/gnu" > "$scratch/gnu.stdout"
"$scratch/minic" > "$scratch/minic.stdout"
cmp "$scratch/gnu.stdout" "$scratch/minic.stdout"
echo 'X86_SYSV_RECORD_ABI=PASS libc_div_t=RAX 32B_record_by_value=STACK GNU_runtime=byte_exact'
