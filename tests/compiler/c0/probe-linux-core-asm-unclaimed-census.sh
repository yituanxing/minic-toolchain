#!/usr/bin/env bash
set -Eeuo pipefail
minic=${1:?expected MiniC executable}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# An authentic-looking LoongArch AMDB opcode with deliberately invalid
# operands must be recorded as a rejected contract, not silently emitted.
# GNU preprocessing follows the same .i interface as real Linux Kbuild.
cc -E -P -x c "$root/tests/compiler/c0/linux-core-asm-unclaimed-census.c" \
    -o "$work/source.i"
if MINIC_ASM_FRONTIER_TRACE=1 "$minic" --target=loongarch64-linux -S \
    "$work/source.i" \
    -o "$work/reject.s" >"$work/stdout" 2>"$work/stderr"; then
    echo 'unqualified LoongArch AMDB machine contract was accepted' >&2
    exit 1
fi
grep -E '^CORE_ASM_FRONTIER function=not_an_isa_contract reason=target_required ' \
    "$work/stderr" >/dev/null
grep -E 'template_hash=[0-9a-f]{16} signature_hash=[0-9a-f]{16}' \
    "$work/stderr" >/dev/null
grep -F 'preview=amswap_db_w_0_0_0 outputs=0 inputs=1' "$work/stderr" >/dev/null
grep -F "Core IR does not yet support function 'not_an_isa_contract'" \
    "$work/stderr" >/dev/null
test ! -s "$work/reject.s"

# Opt-in tracing must have no effect on admission or the normal diagnostic.
if "$minic" --target=loongarch64-linux -S \
    "$work/source.i" \
    -o "$work/normal.s" >"$work/normal.stdout" 2>"$work/normal.stderr"; then
    echo 'unqualified LoongArch AMDB accepted without the trace' >&2
    exit 1
fi
if grep -F 'CORE_ASM_FRONTIER' "$work/normal.stderr"; then
    echo 'opt-in Core ASM census unexpectedly enabled' >&2
    exit 1
fi
grep -F "Core IR does not yet support function 'not_an_isa_contract'" \
    "$work/normal.stderr" >/dev/null
test ! -s "$work/normal.s"
echo 'CORE_ASM_FRONTIER_CENSUS=PASS target=loongarch64 fail_closed=1 stable_signature=1'
