#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-prefix-function-attributes
assembly="$work/gnu_prefix_function_attributes.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_prefix_function_attributes.c" \
    -o "$work/gnu_prefix_function_attributes.i"
"$minic" -S "$work/gnu_prefix_function_attributes.i" -o "$assembly"

test -s "$assembly"
grep -F 'prefix_attribute_identity:' "$assembly" >/dev/null
grep -F 'linux_noinstr_plain:' "$assembly" >/dev/null
grep -F 'linux_noinstr_underscored:' "$assembly" >/dev/null
grep -F 'linux_artificial_calls:' "$assembly" >/dev/null
grep -F 'call_prefix_attribute_identity:' "$assembly" >/dev/null
grep -F 'prefix_external_identity:' "$assembly" >/dev/null
grep -F '.globl prefix_external_identity' "$assembly" >/dev/null
grep -F 'externally_visible_decl:' "$assembly" >/dev/null
grep -F 'instrumentation_policy_aliases:' "$assembly" >/dev/null
grep -F 'zstd_ubsan_policy:' "$assembly" >/dev/null
grep -F 'zstd_ubsan_underscored_policy:' "$assembly" >/dev/null
grep -F 'zstd_pointer_overflow_policy:' "$assembly" >/dev/null
grep -F 'zstd_signed_overflow_policy:' "$assembly" >/dev/null
grep -F 'zstd_visibility_decl:' "$assembly" >/dev/null
grep -F 'zstd_pure_decl:' "$assembly" >/dev/null
grep -F 'zstd_hidden_visibility_definition:' "$assembly" >/dev/null
grep -F '.hidden zstd_hidden_visibility_definition' "$assembly" >/dev/null
grep -F 'prefix_plain_spelling_identity:' "$assembly" >/dev/null
grep -F '.globl externally_visible_decl' "$assembly" >/dev/null
grep -F '.section .probe.externally-visible.text' "$assembly" >/dev/null
grep -F 'externally_visible_object' "$assembly" >/dev/null
if grep -F '.hidden externally_visible_decl' "$assembly" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/gnu_prefix_function_attributes: externally_visible was mapped to ELF hidden visibility' >&2
    exit 1
fi

# Same language-attribute policy across all five targets: the backend only
# owns physical ABI/instructions; no copied per-architecture parser rule.
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$work/gnu_prefix_function_attributes.i" \
        -o "$work/gnu-prefix-$target.s"
    test -s "$work/gnu-prefix-$target.s"
    grep -F 'linux_noinstr_plain:' "$work/gnu-prefix-$target.s" >/dev/null
    grep -F 'linux_artificial_calls:' "$work/gnu-prefix-$target.s" >/dev/null
done

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_extern_inline_attribute.c" \
    -o "$work/gnu_extern_inline_attribute.i"
if "$minic" -S "$work/gnu_extern_inline_attribute.i" \
    -o "$work/gnu_extern_inline_attribute.s" \
    >"$work/gnu_extern_inline_attribute.out" 2>"$work/gnu_extern_inline_attribute.err"; then
    printf '%s\n' 'FAIL compiler/c0/gnu_prefix_function_attributes: extern inline gnu_inline unexpectedly emitted a standalone definition' >&2
    exit 1
fi
grep -F 'GNU extern inline gnu_inline requires inline-only emission semantics' \
    "$work/gnu_extern_inline_attribute.err" >/dev/null

cat >"$work/no-sanitize-invalid.i" <<'SRC'
__attribute__((no_sanitize(123))) int reject_invalid_policy(int x) {
    return x;
}
SRC
if "$minic" -S "$work/no-sanitize-invalid.i" -o "$work/no-sanitize-invalid.s" \
    >"$work/no-sanitize-invalid.out" 2>"$work/no-sanitize-invalid.err"; then
    printf '%s\n' 'FAIL compiler/c0/gnu_prefix_function_attributes: invalid sanitizer argument accepted' >&2
    exit 1
fi
grep -F 'GNU no_sanitize requires one string literal' \
    "$work/no-sanitize-invalid.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_prefix_function_attributes five_target_language_policy=1 prefix=1 metadata=unused,no-instrument,artificial,externally-visible function+object=1 reachability=parse-only public-linkage=preserved gnu-inline=static+bare+plain-spelling always-inline=underscored+plain-spelling extern-inline=fail-closed no-sanitize-address=parse-only no-stack-protector=parse-only'
