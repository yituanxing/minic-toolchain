#!/bin/sh
# Shared positive compile-only oracle: preserve the former per-case flags and work paths.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
if [ "$#" -ne 1 ]; then
    echo "usage: $0 CASE" >&2
    echo "cases: external-array-redeclaration-owner external-tentative-declarator-list gnu-object-unused-alias runtime-record-zero-shorthand small-record-copy-return static-local-prefix-attributes static-record-single-char-array backward-static-aggregate-overlay fixed-call-character-pointer" >&2
    exit 2
fi
case $1 in
    external-array-redeclaration-owner|external-tentative-declarator-list|gnu-object-unused-alias|runtime-record-zero-shorthand|small-record-copy-return|static-local-prefix-attributes|static-record-single-char-array)
        set -- "$1"
        ;;
    backward-static-aggregate-overlay|fixed-call-character-pointer)
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
mkdir -p "$work"
"$host_cc" -E -P "$@" -x c "$root/tests/compiler/c0/$source.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
