#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?set MINIAS to exact-head minic-as}"
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# A CLI client must depend solely on the public invocation API: all target
# profile/ELF details live behind it. A fifth ISA can register one Target
# without updating CLI's private ABI model or a hardcoded help string.
if grep -Eq 'minias_internal\.h|minias_find_target|minias_assemble_config|MiniAsTargetOps' \
        "$repo/tools/minic-as/main.c"; then
    echo 'MiniAS CLI leaked private target/assembler model' >&2
    exit 1
fi
grep -q 'minias_supported_target_name' "$repo/tools/minic-as/main.c"

cc -I"$repo/assembler/src" -std=c11 -Wall -Wextra -Werror \
    -x c -fsyntax-only - <<'C'
#include "minias.h"
int client(const char *in, const char *out, FILE *log) {
    MiniAsInvocation options = {"riscv", "rv64i", "lp64"};
    size_t i;
    for (i = 0; i < minias_supported_target_count(); ++i)
        if (minias_supported_target_name(i) == 0) return 2;
    return minias_assemble_invocation(in, out, &options, log);
}
C

usage=$("$MINIAS" --help)
for target in riscv aarch64 x86_64 loongarch64; do
    if ! grep -q -- "$target" <<<"$usage"; then
        echo "missing registered target from CLI usage: $target" >&2
        exit 1
    fi
    printf '.text\nnop\n' >"$work/$target.s"
    "$MINIAS" --target="$target" \
        -o "$work/$target.o" "$work/$target.s"
    test -s "$work/$target.o"
done

if "$MINIAS" --target=unknown-target \
        -o "$work/invalid.o" "$work/riscv.s" >"$work/out" 2>"$work/err"; then
    echo 'unknown target unexpectedly assembled' >&2
    exit 1
fi
grep -q 'unsupported-target:unknown-target' "$work/err"
test ! -e "$work/invalid.o"
echo 'MINIAS_PUBLIC_INVOCATION=PASS public_api=1 dynamic_registry=4 target_object=4 unknown_target=fail_closed'
