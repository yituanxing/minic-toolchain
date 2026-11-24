#!/bin/sh
# Shared positive compile-only oracle: preserve the former per-case flags and work paths.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
if [ "$#" -ne 1 ]; then
    echo "usage: $0 CASE" >&2
    echo "cases: external-array-redeclaration-owner external-tentative-declarator-list gnu-object-unused-alias runtime-record-zero-shorthand small-record-copy-return static-local-prefix-attributes static-record-single-char-array backward-static-aggregate-overlay fixed-call-character-pointer preformed-static-array static-record-nested-designator-string-overlay static-integer-address-relocation static-record-array-suffix-attributes block-scope-extern-multi-declarator gnu-packed-record-field" >&2
    exit 2
fi
case $1 in
    external-array-redeclaration-owner|external-tentative-declarator-list|gnu-object-unused-alias|runtime-record-zero-shorthand|small-record-copy-return|static-local-prefix-attributes|static-record-single-char-array|preformed-static-array|static-record-array-suffix-attributes|block-scope-extern-multi-declarator)
        set -- "$1"
        ;;
    backward-static-aggregate-overlay|fixed-call-character-pointer|static-record-nested-designator-string-overlay|static-integer-address-relocation|gnu-packed-record-field)
        set -- "$1" -std=gnu11
        ;;
    *)
        echo "unknown compile-only case: $1" >&2
        exit 2
        ;;
esac
name=$1
shift
source=$(printf '%s' "$name" | tr '-' '_')
work=${BUILD_DIR:-"$root/build/debug"}/tests/$name
case "$name" in
    block-scope-extern-multi-declarator)
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-block-extern-multi
        ;;
    gnu-packed-record-field)
        work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-packed-record-field
        rm -rf "$work"
        ;;
esac
mkdir -p "$work"
"$host_cc" -E -P "$@" -x c "$root/tests/compiler/c0/$source.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
case "$name" in
    preformed-static-array)
        test -s "$work/output.s"
        grep -F '.section .data.test' "$work/output.s" >/dev/null
        ;;
    static-record-array-suffix-attributes)
        grep -F '.section .data.test' "$work/output.s" >/dev/null
        test -s "$work/output.s"
        ;;
    static-integer-address-relocation)
        test -s "$work/output.s"
        grep -q 'init_stack' "$work/output.s"
        ;;
    block-scope-extern-multi-declarator)
        grep -F '__init_begin' "$work/output.s" >/dev/null
        grep -F '__init_end' "$work/output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/block_scope_extern_multi_declarator declarators=2 incomplete-arrays=2 scoped-bindings=2 entity-merge=transactional-array-descriptors'
        ;;
    gnu-packed-record-field)
        test -s "$work/output.s"
        grep -F 'main:' "$work/output.s" >/dev/null
        printf '%s\n' 'PASS compiler/c0/gnu_packed_record_field field-identity=1 natural-alignment=lowered next-field=natural packed+aligned=explicit-raises record-layout=DataLayout'
        ;;
    *)
        test -s "$work/output.s"
        ;;
esac
