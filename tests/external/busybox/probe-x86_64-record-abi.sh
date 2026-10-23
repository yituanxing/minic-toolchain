#!/usr/bin/env bash
set -Eeuo pipefail
if [[ $# != 1 ]]; then echo "usage: $0 <minic-cc>" >&2; exit 64; fi
compiler=$(realpath "$1")
scratch=${RUNNER_TEMP:-/tmp}/minic-x86-record-abi
mkdir -p "$scratch"
cat > "$scratch/abi.c" <<'SOURCE'
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
/* Upstream libsepol callback argument: format annotates the function type,
 * not the pointer-object. Parse the original GNU function-pointer grammar. */
extern void bind_callback(void (*fn)(void *, const char *, ...)
    __attribute__((format(printf, 2, 3))), void *context);
static const char *const_provider(void) { return "GNU"; }
/* A copied SysV cursor must NOT alias the cursor later advanced by va_arg.
 * Checking the second element catches even a superficially correct memcpy
 * of only the first argument's apparent register position. */
static int check_va_copy(int marker, ...)
{
    va_list first, second;
    long a, b, next_a, next_b;
    va_start(first, marker);
    va_copy(second, first);
    a = va_arg(first, long);
    next_a = va_arg(first, long);
    b = va_arg(second, long);
    next_b = va_arg(second, long);
    va_end(second);
    va_end(first);
    return marker == 7 && a == 17 && next_a == 19 &&
           b == 17 && next_b == 19;
}
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
    char *legacy_security_context;
    legacy_security_context = const_provider();
    x.a = 11; x.b = 13; x.c = 17;
    x.tail[0] = 2; x.tail[1] = 3; x.tail[2] = 4;
    x.tail[3] = 5; x.tail[4] = 7;
    printf("sysv-record %d:%d %lu %c\n", pair.quot, pair.rem,
           receive_by_value(x, 19), legacy_security_context[0]);
    return (!check_va_copy(7, 17L, 19L) ||
            legacy_security_context[0] != 'G' ||
            pair.quot != 11 || pair.rem != 4
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
# A one-level const conversion (GNU warning) must not accidentally admit
# pointer-to-pointer qualification drops or discard volatile qualifiers.
cat > "$scratch/invalid.c" <<'NEGATIVE'
void bad(char **pp, const char **c, volatile const char *v, char *p) {
    pp = c;
    p = v;
}
NEGATIVE
cc -E -P -x c "$scratch/invalid.c" -o "$scratch/invalid.i"
if "$compiler" --target=x86_64-linux -S "$scratch/invalid.i" -o "$scratch/invalid.s" \
     >"$scratch/invalid.stdout" 2>"$scratch/invalid.stderr"; then
  echo "X86_GNU_CONST_DISCARD_NEGATIVE=FAIL illegal_pointer_conversion_admitted" >&2
  exit 1
fi
echo 'X86_GNU_CONST_DISCARD_NEGATIVE=PASS unsafe_pointer_conversions_rejected'
echo 'X86_SYSV_RECORD_ABI=PASS libc_div_t=RAX 32B_record_by_value=STACK GNU_const_pointer_compat=1 callback_format_attribute=1 sysv_va_copy_24B=1 runtime_byte_exact=1'
