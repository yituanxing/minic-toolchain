#!/usr/bin/env bash
# Macro error paths must release their sole SourceContext-owned buffers and
# fail closed without publishing an object; positive macro tests live in A0.
set -Eeuo pipefail
: "$MINIAS"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/args.s" <<'SRC'
.macro m a
.byte \a
.endm
m 1,2
SRC
cat >"$work/keyword.s" <<'SRC'
.macro m a,b
.byte \a
.endm
m a=1,a=2
SRC
cat >"$work/vararg.s" <<'SRC'
.macro m a,b:vararg
.byte \a
.endm
m 1,b=2,3
SRC
cat >"$work/required.s" <<'SRC'
.macro m a:req
.byte \a
.endm
m
SRC
cat >"$work/recursion.s" <<'SRC'
.macro m
m
.endm
m
SRC
cat >"$work/altpercent.s" <<'SRC'
.altmacro
.macro m a
.byte \a
.endm
m %nonsense
SRC

for case_name in args keyword vararg required recursion altpercent; do
    case "$case_name" in
        args) expected='bad-macro-arguments';;
        keyword) expected='bad-macro-keyword';;
        vararg) expected='duplicate-macro-vararg';;
        required) expected='missing-macro-argument';;
        recursion) expected='macro-expansion-depth';;
        altpercent) expected='bad-altmacro-percent';;
    esac
    if "$MINIAS" --target=x86_64 -o "$work/$case_name.o" \
          "$work/$case_name.s" >"$work/$case_name.out" \
          2>"$work/$case_name.err"; then
        echo "MINIAS_MACRO_FAILURE_CLEANUP=FAIL unexpectedly_accepted=$case_name" >&2
        exit 1
    fi
    if [[ -e "$work/$case_name.o" ]]; then
        echo "MINIAS_MACRO_FAILURE_CLEANUP=FAIL partial_object=$case_name" >&2
        exit 1
    fi
    if ! grep -Fq "$expected" "$work/$case_name.err"; then
        echo "MINIAS_MACRO_FAILURE_CLEANUP=FAIL wrong_diagnostic=$case_name" >&2
        cat "$work/$case_name.err" >&2
        exit 1
    fi
done
echo 'MINIAS_MACRO_FAILURE_CLEANUP=PASS invalid_paths=6 no_partial_objects=1'
