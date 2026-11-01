#!/usr/bin/env bash
# MC-044: parameter-list tag scope must match GNU C, including definition bodies.
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
fail() { echo "MC044_PARAMETER_TAG_SCOPE=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC binary missing'
input="$root/tests/compiler/audit/mc044_parameter_tag_scope.c"
targets=(x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux)

for variant in 1 2 3 4 5 6 7 8 9 11 12 13 14 15; do
    "$cc" -std=gnu11 -E -P -DMC044_VARIANT="$variant" "$input" \
        -o "$work/$variant.i" || fail "case=$variant GNU preprocess"
    "$cc" -std=gnu11 -Wno-visibility -O0 -no-pie -x c "$work/$variant.i" \
        -o "$work/$variant.gnu" 2>"$work/$variant.gnu.log" ||
        fail "case=$variant GNU native compile"
    for target in "${targets[@]}"; do
        "$minic" --target="$target" -S "$work/$variant.i" \
            -o "$work/$variant.$target.s" ||
            fail "case=$variant MiniC target=$target -S"
    done
    "$cc" -no-pie -x assembler "$work/$variant.x86_64-linux.s" \
        -o "$work/$variant.minic" || fail "case=$variant MiniC native link"
    gnu_status=0
    minic_status=0
    "$work/$variant.gnu" || gnu_status=$?
    "$work/$variant.minic" || minic_status=$?
    test "$gnu_status" -eq 7 || fail "case=$variant GNU=$gnu_status expected=7"
    test "$minic_status" -eq "$gnu_status" ||
        fail "case=$variant GNU=$gnu_status MiniC=$minic_status"
done

# Identically spelled tags in two independent function prototype scopes have
# distinct identities. The old global leak made this conflicting declaration
# appear compatible; both compilers must reject it after the scope fix.
"$cc" -std=gnu11 -E -P -DMC044_VARIANT=10 "$input" -o "$work/10.i" ||
    fail 'negative GNU preprocess'
if "$cc" -std=gnu11 -fsyntax-only -x c "$work/10.i" \
    >"$work/10.gnu.log" 2>&1; then
    fail 'negative GNU unexpectedly accepted incompatible prototype tags'
fi
for target in "${targets[@]}"; do
    if "$minic" --target="$target" -S "$work/10.i" -o "$work/10.$target.s" \
        >"$work/10.$target.log" 2>&1; then
        fail "negative MiniC target=$target accepted incompatible prototype tags"
    fi
    if ! grep -q 'conflicting function declaration' "$work/10.$target.log"; then
        fail "negative MiniC target=$target failed for an unrelated reason"
    fi
done

echo 'MC044_PARAMETER_TAG_SCOPE=PASS 14_positive_5target_S_GNU_MiniC_native_7_and_5target_negative'
