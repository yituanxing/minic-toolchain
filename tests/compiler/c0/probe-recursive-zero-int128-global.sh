#!/usr/bin/env bash
# Recursive aggregate initializer: an unsigned/signed __int128 zero consumes
# one logical scalar slot but occupies sixteen physical bytes. Preserve the
# next nested pointer relocation and reject nonzero payload truncation.
set -Eeuo pipefail
mini=${1:?MiniC compiler path}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/zero.i" <<'SOURCE'
extern int external_target;
struct inner { unsigned __int128 zero; void *pointer; signed __int128 signed_zero; };
struct outer { int prefix; struct inner data; int suffix; };
struct outer global_state = {
    .prefix = 13,
    .data = { .pointer = &external_target },
    .suffix = 41
};
SOURCE

for target in aarch64 loongarch64; do
    "$mini" --target="$target"-linux -S "$work/zero.i" -o "$work/$target.s"
    test "$(grep -Fc '.zero 16' "$work/$target.s")" -eq 2
    grep -Fq '.8byte external_target' "$work/$target.s"
    grep -Fq 'global_state:' "$work/$target.s"
    if [[ "${MINIC_RECURSIVE_INT128_SOURCE_ONLY:-0}" != 1 ]]; then
        as_tool=${target}-linux-gnu-as
        command -v "$as_tool" >/dev/null
        "$as_tool" -o "$work/$target.o" "$work/$target.s"
    fi
done

cat > "$work/nonzero.i" <<'SOURCE'
struct payload { unsigned __int128 full; };
struct payload global_nonzero = { .full = 1 };
SOURCE
for target in aarch64 loongarch64; do
    if "$mini" --target="$target"-linux -S "$work/nonzero.i" \
        -o "$work/bad.s" >"$work/bad.out" 2>&1; then
        printf 'nonzero 128-bit scalar was silently truncated on %s\n' "$target" >&2
        exit 1
    fi
    test ! -e "$work/bad.s"
done
echo "MINIC_RECURSIVE_INT128_GLOBAL=PASS targets=2 zero_payloads=4 pointer_relocs=2 nonzero_rejected=2 source_only=${MINIC_RECURSIVE_INT128_SOURCE_ONLY:-0}"
