#!/bin/sh
# A preflight 16-parameter cap used to hide the real zstd 18-argument function
# despite the common LP64 function/call ABI planner accepting up to 64.
# Exercise both incoming stack parameters and caller stack parameter slots.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

generate() {
    count=$1
    printf 'long many%s(' "$count"
    i=1
    while [ "$i" -le "$count" ]; do
        [ "$i" -eq 1 ] || printf ', '
        printf 'long p%s' "$i"
        i=$((i + 1))
    done
    printf ') { return p1 + p8 + p9 + p%s; }\n' "$count"
    printf 'long call%s(void) { return many%s(' "$count" "$count"
    i=1
    while [ "$i" -le "$count" ]; do
        [ "$i" -eq 1 ] || printf ', '
        printf '%s' "$i"
        i=$((i + 1))
    done
    printf '); }\n'
}

generate 18 >"$work/many.i"
generate 32 >>"$work/many.i"
generate 64 >>"$work/many.i"
for arch in loongarch64-linux aarch64-linux; do
    "$MINIC_CC" --target="$arch" -fPIE -S \
        "$work/many.i" -o "$work/$arch.s"
    for n in 18 32 64; do
        grep -Eq "(^|[[:space:]])many${n}:" "$work/$arch.s"
        grep -Eq "(^|[[:space:]])call${n}:" "$work/$arch.s"
    done
    case "$arch" in
        loongarch64-linux)
            grep -Fq '  bl many64' "$work/$arch.s"
            grep -Fq '  st.d $t0,' "$work/$arch.s"
            ;;
        aarch64-linux)
            grep -Fq '  bl many64' "$work/$arch.s"
            grep -Fq '  str x9, [sp, #0]' "$work/$arch.s"
            ;;
    esac
    echo "MINIC_LP64_MANY_PARAMETERS=PASS arch=$arch arities=18,32,64 caller=1 callee=1"
done

generate 65 >"$work/invalid.i"
for arch in loongarch64-linux aarch64-linux; do
    if "$MINIC_CC" --target="$arch" -fPIE -S \
        "$work/invalid.i" -o "$work/invalid-$arch.s" \
        >/dev/null 2>"$work/invalid-$arch.err"; then
        echo "MINIC_LP64_MANY_PARAMETERS=FAIL arch=$arch arity=65 accepted" >&2
        exit 1
    fi
    test ! -e "$work/invalid-$arch.s"
done
echo 'MINIC_LP64_MANY_PARAMETERS_NEGATIVE=PASS arity=65 rejected'
