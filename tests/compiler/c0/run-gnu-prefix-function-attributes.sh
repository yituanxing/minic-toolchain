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
grep -F 'minic_probe_const_underscored:' "$assembly" >/dev/null
grep -F 'minic_probe_const_plain:' "$assembly" >/dev/null
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
grep -F 'call_prefix_plain_spelling_identity:' "$assembly" >/dev/null
grep -F '.globl externally_visible_decl' "$assembly" >/dev/null
grep -F '.section .probe.externally-visible.text' "$assembly" >/dev/null
grep -F 'externally_visible_object' "$assembly" >/dev/null
if grep -F '.hidden externally_visible_decl' "$assembly" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/gnu_prefix_function_attributes: externally_visible was mapped to ELF hidden visibility' >&2
    exit 1
fi

# The full GNU prefix fixture deliberately also exercises function
# sections, visibility, pointer aggregates and GNU extern-inline semantics,
# which do NOT yet have full backend qualification on all five targets.
# Cross-target checks therefore isolate only the shared attribute semantics.
# We must not weaken the original RV64 full fixture or add target-specific
# frontend hacks just to pass unrelated machine emitter gaps.
cat > "$work/gnu-shared-metadata.i" <<'SRC'
__attribute__((no_instrument_function))
int shared_noinstr(int value) { return value + 1; }
__attribute__((__no_instrument_function__))
int shared_noinstr_under(int value) { return value - 1; }
static inline __attribute__((artificial))
int shared_artificial(int value) { return value + 2; }
static inline __attribute__((__artificial__))
int shared_artificial_under(int value) { return value - 2; }
int shared_metadata_use(int value) {
    return shared_artificial(value) + shared_artificial_under(value);
}
extern void *memcpy(void *__to, __const__ void *__from, unsigned long __n);
int shared_const_qualified(__const__ int *p) { return *p; }
int shared_const_plain(__const int *p) { return *p; }
SRC
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$work/gnu-shared-metadata.i" \
        -o "$work/gnu-prefix-$target.s"
    test -s "$work/gnu-prefix-$target.s"
    grep -F 'shared_noinstr:' "$work/gnu-prefix-$target.s" >/dev/null
    grep -F 'shared_metadata_use:' "$work/gnu-prefix-$target.s" >/dev/null
    grep -F 'shared_const_qualified:' "$work/gnu-prefix-$target.s" >/dev/null
    grep -F 'shared_const_plain:' "$work/gnu-prefix-$target.s" >/dev/null
done

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_extern_inline_attribute.c" \
    -o "$work/gnu_extern_inline_attribute.i"
"$minic" -S "$work/gnu_extern_inline_attribute.i" \
    -o "$work/gnu_extern_inline_attribute.s" \
    >"$work/gnu_extern_inline_attribute.out" 2>"$work/gnu_extern_inline_attribute.err"
test -s "$work/gnu_extern_inline_attribute.s"
# The existing shared parser now supports true GNU extern inline: a body
# is syntax-checked but must not become a second externally strong symbol.
# The old test demanded a compiler failure even for valid implementation.
if grep -F 'prefix_extern_inline_identity:' "$work/gnu_extern_inline_attribute.s" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/gnu_prefix_function_attributes: GNU extern inline produced a strong definition' >&2
    exit 1
fi

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

printf '%s\n' 'PASS compiler/c0/gnu_prefix_function_attributes five_target_language_policy=1 gnu_const_aliases=5_targets prefix=1 metadata=unused,no-instrument,artificial,externally-visible function+object=1 reachability=parse-only public-linkage=preserved gnu-inline=static+bare+plain-spelling always-inline=underscored+plain-spelling extern-inline=fail-closed no-sanitize-address=parse-only no-stack-protector=parse-only'
