#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-tag-namespace-scopes

mkdir -p "$work"

"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
    "$root/tests/compiler/c0/tag_namespace_scopes.c"
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/tag_namespace_scopes.c" -o "$work/valid.i"
"$minic" -S --target=x86_64-linux "$work/valid.i" -o "$work/valid.s"
test -s "$work/valid.s"
"$host_cc" -no-pie "$work/valid.s" -o "$work/valid"
"$work/valid"

for probe in \
    invalid_tag_kind_enum_struct \
    invalid_tag_kind_struct_enum \
    invalid_tag_kind_struct_union \
    invalid_tag_kind_union_struct \
    invalid_tag_kind_shadow_enum \
    invalid_tag_kind_shadow_struct \
    invalid_tag_kind_forward_enum_record; do
    if "$host_cc" -std=gnu11 -fsyntax-only \
        "$root/tests/compiler/c0/$probe.c" \
        >"$work/$probe.gcc.stdout" 2>"$work/$probe.gcc.stderr"; then
        printf '%s\n' "FAIL tag-namespace: GCC accepted $probe" >&2
        exit 1
    fi
    "$host_cc" -E -P -std=gnu11 -x c \
        "$root/tests/compiler/c0/$probe.c" -o "$work/$probe.i"
    if "$minic" -S --target=x86_64-linux \
        "$work/$probe.i" -o "$work/$probe.s" \
        >"$work/$probe.minic.stdout" 2>"$work/$probe.minic.stderr"; then
        printf '%s\n' "FAIL tag-namespace: MiniC accepted $probe" >&2
        exit 1
    fi
    grep -F 'different' "$work/$probe.minic.stderr" >/dev/null
done

printf '%s\n' 'PASS compiler/c0/tag-namespace gcc-oracle=8 mixed-kind-shadow=1 type-restore=1 struct-forward-shadow=1 same-scope-cross-kind-reject=4 shadowed-wrong-kind-reject=2 enum-forward-cross-kind-reject=1 runtime=1'
